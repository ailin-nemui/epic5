/*
 * functions.c -- Built-in functions for ircII
 *
 * Written by Michael Sandrof
 * Copyright(c) 1990 Michael Sandrof
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * Written by Jeremy Nelson
 * With help from many contributers, including:
 *	Matt Carothers 			Colten Edwards
 *	James Sneeringer		Eli Sand
 * Copyright 1997 EPIC Software Labs
 *
 * I split alias.c because it was just getting out of hand.
 */

#include "irc.h"
#include "alias.h"
#include "alist.h"
#include "array.h"
#include "dcc.h"
#include "commands.h"
#include "files.h"
#include "ignore.h"
#include "input.h"
#include "ircaux.h"
#include "names.h"
#include "output.h"
#include "parse.h"
#include "screen.h"
#include "server.h"
#include "status.h"
#include "vars.h"
#include "window.h"
#include "term.h"
#include "notify.h"
#include "numbers.h"
#include "crypt.h"
#include "options"

#include <sys/stat.h>
#ifdef HAVE_REGEX_H
# include <regex.h>
#endif
#ifdef HAVE_UNAME
# include <sys/utsname.h>
#endif
#ifdef NEED_GLOB
# include "glob.h"
#else
# include <glob.h>
#endif
#include <math.h>


static	char	
	*alias_detected 	(void), *alias_sent_nick 	(void),
	*alias_recv_nick 	(void), *alias_msg_body 	(void),
	*alias_joined_nick 	(void), *alias_public_nick 	(void),
	*alias_dollar 		(void), *alias_channel 		(void),
	*alias_server 		(void), *alias_query_nick 	(void),
	*alias_target 		(void), *alias_nick 		(void),
	*alias_invite 		(void), *alias_cmdchar 		(void),
	*alias_line 		(void), *alias_away 		(void),
	*alias_oper 		(void), *alias_chanop 		(void),
	*alias_modes 		(void), *alias_buffer 		(void),
	*alias_time 		(void), *alias_version 		(void),
	*alias_currdir 		(void), *alias_current_numeric	(void),
	*alias_server_version 	(void), *alias_show_userhost 	(void),
	*alias_show_realname 	(void), *alias_online 		(void),
	*alias_idle 		(void), *alias_version_str 	(void),
	*alias_banner		(void);

typedef struct
{
	char	name;
	char	*(*func) (void);
}	BuiltIns;

static	BuiltIns built_in[] =
{
	{ '.',		alias_sent_nick 	},
	{ ',',		alias_recv_nick 	},
	{ ':',		alias_joined_nick 	},
	{ ';',		alias_public_nick 	},
	{ '$',		alias_dollar 		},
	{ 'A',		alias_away 		},
	{ 'B',		alias_msg_body 		},
	{ 'C',		alias_channel 		},
	{ 'D',		alias_detected 		},
	{ 'E',		alias_idle 		},
	{ 'F',		alias_online 		},
	{ 'G',		alias_banner		},
	{ 'H', 		alias_current_numeric 	},
	{ 'I',		alias_invite 		},
	{ 'J',		alias_version_str 	},
	{ 'K',		alias_cmdchar 		},
	{ 'L',		alias_line 		},
	{ 'M',		alias_modes 		},
	{ 'N',		alias_nick 		},
	{ 'O',		alias_oper 		},
	{ 'P',		alias_chanop 		},
	{ 'Q',		alias_query_nick 	},
	{ 'R',		alias_server_version 	},
	{ 'S',		alias_server 		},
	{ 'T',		alias_target 		},
	{ 'U',		alias_buffer 		},
	{ 'V',		alias_version 		},
	{ 'W',		alias_currdir 		},
	{ 'X',		alias_show_userhost 	},
	{ 'Y',		alias_show_realname 	},
	{ 'Z',		alias_time 		},
	{ 0,	 	NULL 			}
};

/* the 30 "standard" functions */
static	char
	*function_channels 	(char *), 
	*function_connect 	(char *),
	*function_curpos 	(char *), 
	*function_decode 	(unsigned char *),
	*function_encode 	(unsigned char *),
	*function_index 	(char *), 
	*function_ischannel 	(char *),
	*function_ischanop 	(char *), 
	*function_left 		(char *),
	*function_listen 	(char *),
	*function_match 	(char *),
	*function_mid 		(char *),
	*function_pid 		(char *),
	*function_ppid 		(char *),
	*function_rand 		(char *),
	*function_right 	(char *),
	*function_rindex 	(char *),
	*function_rmatch 	(char *),
	*function_servers 	(char *),
	*function_srand 	(char *),
	*function_stime 	(char *),
	*function_strip 	(char *),
	*function_tdiff 	(char *),
	*function_tdiff2 	(char *),
	*function_time 		(char *),
	*function_tolower 	(char *),
	*function_toupper 	(char *),
	*function_userhost 	(char *),
	*function_winnum 	(char *),
	*function_winnam 	(char *),
	*function_word 		(char *),
	*function_utime		(char *),
	*function_strftime	(char *),

/* the countless "extended" functions */
	*function_abs		(char *),
	*function_acos		(char *),
	*function_asin		(char *),
	*function_atan		(char *),
	*function_acosh		(char *),
	*function_asinh		(char *),
	*function_atanh		(char *),
	*function_after 	(char *),
	*function_afterw 	(char *),
	*function_aliasctl	(char *),
	*function_ascii 	(char *),
	*function_before 	(char *),
	*function_beforew 	(char *),
	*function_builtin	(char *),
	*function_ceil		(char *),
	*function_center 	(char *),
	*function_cexist	(char *),
	*function_channel	(char *),
	*function_channelmode	(char *),
	*function_chmod		(char *),
	*function_chngw 	(char *),
	*function_chop		(char *),
	*function_chops 	(char *),
	*function_chr 		(char *),
	*function_cipher	(char *),
	*function_close 	(char *),
	*function_common 	(char *),
	*function_convert 	(char *),
	*function_copattern 	(char *),
	*function_corpattern 	(char *),
	*function_cos		(char *),
	*function_cosh		(char *),
	*function_count		(char *),
	*function_cparse	(char *),
	*function_crypt 	(char *),
	*function_currchans	(char *),
	*function_deuhc		(char *),
	*function_diff 		(char *),
	*function_encryptparm 	(char *),
	*function_eof 		(char *),
	*function_epic		(char *),
	*function_error		(char *),
	*function_exp		(char *),
	*function_fnexist	(char *),
	*function_fexist 	(char *),
	*function_filter 	(char *),
	*function_findw		(char *),
	*function_floor		(char *),
	*function_fromw 	(char *),
	*function_fsize	 	(char *),
	*function_ftime		(char *),
	*function_functioncall	(char *),
	*function_geom		(char *),
	*function_getcap	(char *),
	*function_getcommands	(char *),
	*function_getenv	(char *),
	*function_getgid	(char *),
	*function_getlogin	(char *),
	*function_getopt	(char *),
	*function_getpgrp	(char *),
	*function_getset	(char *),
	*function_getsets	(char *),
	*function_getuid	(char *),
	*function_glob		(char *),
	*function_globi		(char *),
	*function_hash_32bit	(char *),
	*function_idle		(char *),
	*function_igmask	(char *),
	*function_igtype	(char *),
	*function_indextoword	(char *),
	*function_info		(char *),
	*function_insert 	(char *),
	*function_insertw 	(char *),
	*function_iptolong	(char *),
	*function_iptoname 	(char *),
	*function_irclib	(char *),
	*function_isalpha 	(char *),
	*function_isaway	(char *),
	*function_ischanvoice	(char *),
	*function_isconnected	(char *),
	*function_iscurchan	(char *),
	*function_isdigit 	(char *),
	*function_isdisplaying	(char *),
	*function_isencrypted	(char *),
	*function_isfilevalid	(char *),
	*function_ishalfop	(char *),
	*function_isnumber	(char *),
	*function_jn		(char *),
	*function_jot 		(char *),
	*function_key 		(char *),
	*function_lastserver	(char *),
	*function_leftpc	(char *),
	*function_leftw 	(char *),
	*function_loadinfo	(char *),
	*function_log		(char *),
	*function_log10		(char *),
	*function_longtoip	(char *),
	*function_mask		(char *),
	*function_maxlen	(char *),
	*function_midw 		(char *),
	*function_mkdir		(char *),
	*function_msar		(char *),
	*function_nametoip 	(char *),
	*function_nochops 	(char *),
	*function_nohighlight	(char *),
	*function_notify	(char *),
	*function_notifywindows	(char *),
	*function_notw 		(char *),
	*function_numonchannel 	(char *),
	*function_numsort	(char *),
	*function_numwords 	(char *),
	*function_onchannel 	(char *),
	*function_open 		(char *),
	*function_pad		(char *),
	*function_pattern 	(char *),
	*function_pass		(char *),
#ifdef PERL
	*function_perl		(char *),
	*function_perlcall	(char *),
	*function_perlxcall	(char *),
#endif
	*function_prefix	(char *),
	*function_printlen	(char *),
	*function_querywin	(char *),
	*function_randread	(char *),
	*function_read 		(char *),
	*function_realpath	(char *),
	*function_regcomp	(char *),
	*function_regexec	(char *),
	*function_regerror	(char *),
	*function_regfree	(char *),
	*function_regmatches	(char *),
	*function_remw 		(char *),
	*function_remws		(char *),
	*function_rename 	(char *),
	*function_repeat	(char *),
	*function_rest		(char *),
	*function_restw 	(char *),
	*function_reverse 	(char *),
	*function_revw 		(char *),
	*function_rewind	(char *),
	*function_rfilter 	(char *),
	*function_rightw 	(char *),
	*function_rigmask	(char *),
	*function_rigtype	(char *),
	*function_rmdir 	(char *),
	*function_rpattern 	(char *),
	*function_rsubstr	(char *),
	*function_sar 		(char *),
	*function_seek		(char *),
	*function_server_version (char *),
	*function_servergroup	(char *),
	*function_servername	(char *),
	*function_servernick	(char *),
	*function_servernum	(char *),
	*function_serverourname	(char *),
	*function_servports	(char *),
	*function_sin		(char *),
	*function_sinh		(char *),
	*function_skip		(char *),
	*function_sort		(char *),
	*function_split 	(char *),
	*function_splice 	(char *),
	*function_ssl		(char *),
	*function_stat		(char *),
	*function_status	(char *),
	*function_stripansi	(char *),
	*function_stripansicodes(char *),
	*function_stripc 	(char *),
	*function_stripcrap	(char *),
	*function_strlen	(char *),
	*function_substr	(char *),
	*function_tan		(char *),
	*function_tanh		(char *),
	*function_tow 		(char *),
	*function_translate 	(char *),
	*function_truncate 	(char *),
	*function_ttyname	(char *),
	*function_twiddle	(char *),
	*function_uhc		(char *),
	*function_umask		(char *),
	*function_umode		(char *),
	*function_uname		(char *),
	*function_uniq		(char *),
	*function_unlink 	(char *),
	*function_unsplit	(char *),
	*function_urldecode	(char *),
	*function_urlencode	(char *),
	*function_winbound	(char *),
	*function_which 	(char *),
	*function_winchan	(char *),
	*function_wincurline	(char *),
	*function_winlevel	(char *),
	*function_winnames	(char *),
	*function_winrefs	(char *),
	*function_winsbsize	(char *),
	*function_winscreen	(char *),
	*function_winserv	(char *),
	*function_winsize	(char *),
	*function_winstatsize	(char *),
	*function_winvisible	(char *),
	*function_wordtoindex	(char *),
	*function_write 	(char *),
	*function_writeb	(char *),
	*function_yn		(char *);

extern char
	*function_push		(char *),
	*function_pop		(char *),
	*function_shift		(char *),
	*function_unshift	(char *);

typedef char *(bf) (char *);
typedef struct
{
	const char	*name;
	bf 		*func;
}	BuiltInFunctions;


/* 
 * This is the built-in function list.  This list *must* be sorted because
 * it is binary searched.   See the code for each function to see how it
 * is used.  Or see the help files.  Or see both.  Oh heck.  Look at the code
 * and see how it REALLY works, regardless of the documentation >;-)
 */
static BuiltInFunctions	built_in_functions[] =
{
	{ "ABS",		function_abs		},
	{ "ACOS",		function_acos		},
	{ "ACOSH",		function_acosh		},
	{ "AFTER",              function_after 		},
	{ "AFTERW",             function_afterw 	},
	{ "ALIASCTL",		function_aliasctl	},
	{ "ASCII",              function_ascii 		},
	{ "ASIN",		function_asin		},
	{ "ASINH",		function_asinh		},
	{ "ATAN",		function_atan		},
	{ "ATANH",		function_atanh		},
	{ "BEFORE",             function_before 	},
	{ "BEFOREW",            function_beforew 	},
	{ "BUILTIN_EXPANDO",	function_builtin	},
	{ "CEIL",		function_ceil	 	},
	{ "CENTER",		function_center 	},
	{ "CEXIST",		function_cexist		},
	{ "CHANMODE",		function_channelmode	},
	{ "CHANNEL",		function_channel	},
	{ "CHANUSERS",		function_onchannel 	},
	{ "CHANWIN",		function_winchan	},
	{ "CHMOD",		function_chmod		},
	{ "CHNGW",              function_chngw 		},
	{ "CHOP",		function_chop		},
	{ "CHOPS",              function_chops 		},
	{ "CHR",                function_chr 		},
	{ "CIPHER",		function_cipher		},
	{ "CLOSE",		function_close 		},
	{ "COMMON",             function_common 	},
	{ "CONNECT",		function_connect 	},
	{ "CONVERT",		function_convert 	},
	{ "COPATTERN",          function_copattern 	},
	{ "CORPATTERN",		function_corpattern 	},
	{ "COS",		function_cos		},
	{ "COSH",		function_cosh		},
	{ "COUNT",		function_count		},
	{ "CPARSE",		function_cparse		},
	{ "CRYPT",		function_crypt		},
	{ "CURPOS",		function_curpos 	},
	{ "CURRCHANS",		function_currchans	},
	{ "DECODE",	  (bf *)function_decode 	},
	{ "DELARRAY",           function_delarray 	},
	{ "DELITEM",            function_delitem 	},
	{ "DEUHC",		function_deuhc		},
	{ "DIFF",               function_diff 		},
	{ "ENCODE",	  (bf *)function_encode 	},
	{ "ENCRYPTPARM",	function_encryptparm	},
	{ "EOF",		function_eof 		},
	{ "EPIC",		function_epic		},
	{ "EXP",		function_exp		},
	{ "FERROR",		function_error		},
	{ "FEXIST",             function_fexist 	},
	{ "FILTER",             function_filter 	},
	{ "FINDITEM",           function_finditem 	},
	{ "FINDW",		function_findw		},
	{ "FLOOR",		function_floor		},
	{ "FNEXIST",		function_fnexist	},
	{ "FREWIND",		function_rewind		},
	{ "FROMW",              function_fromw 		},
	{ "FSEEK",		function_seek		},
	{ "FSIZE",		function_fsize		},
	{ "FSKIP",		function_skip		},
	{ "FTIME",		function_ftime		},
	{ "FUNCTIONCALL",	function_functioncall	},
	{ "GEOM",		function_geom		},
	{ "GETARRAYS",          function_getarrays 	},
	{ "GETCAP",		function_getcap		},
	{ "GETCOMMANDS",	function_getcommands	},
	{ "GETENV",		function_getenv		},
	{ "GETGID",		function_getgid		},
	{ "GETITEM",            function_getitem 	},
	{ "GETLOGIN",		function_getlogin	},
	{ "GETMATCHES",         function_getmatches 	},
	{ "GETOPT",		function_getopt		},
	{ "GETPGRP",		function_getpgrp	},
	{ "GETRMATCHES",        function_getrmatches 	},
	{ "GETSET",		function_getset		},
	{ "GETSETS",		function_getsets	},
	{ "GETTMATCH",		function_gettmatch	},
	{ "GETUID",		function_getuid		},
	{ "GLOB",		function_glob		},
	{ "GLOBI",		function_globi		},
	{ "HASH_32BIT",		function_hash_32bit	},
	{ "IDLE",		function_idle		},
	{ "IFINDFIRST",         function_ifindfirst 	},
	{ "IFINDITEM",          function_ifinditem 	},
	{ "IGETITEM",           function_igetitem 	},
	{ "IGETMATCHES",	function_igetmatches	},
	{ "IGETRMATCHES",	function_igetrmatches	},
	{ "IGMASK",		function_igmask		},
	{ "IGTYPE",		function_igtype		},
	{ "INDEX",		function_index 		},
	{ "INDEXTOITEM",        function_indextoitem 	},
	{ "INDEXTOWORD",	function_indextoword	},
	{ "INFO",		function_info		},
	{ "INSERT",		function_insert 	},
	{ "INSERTW",            function_insertw 	},
	{ "IPTOLONG",		function_iptolong	},
	{ "IPTONAME",		function_iptoname 	},
	{ "IRCLIB",		function_irclib		},
	{ "ISALPHA",		function_isalpha 	},
	{ "ISAWAY",		function_isaway		},
	{ "ISCHANNEL",		function_ischannel 	},
	{ "ISCHANOP",		function_ischanop 	},
	{ "ISCHANVOICE",	function_ischanvoice	},
	{ "ISCONNECTED",	function_isconnected	},
	{ "ISCURCHAN",		function_iscurchan	},
	{ "ISDIGIT",		function_isdigit 	},
	{ "ISDISPLAYING",	function_isdisplaying	},
	{ "ISENCRYPTED",	function_isencrypted	},
	{ "ISFILEVALID",	function_isfilevalid	},
	{ "ISHALFOP",		function_ishalfop	},
	{ "ISNUMBER",		function_isnumber	},
	{ "ITEMTOINDEX",        function_itemtoindex 	},
	{ "JN",			function_jn		},
	{ "JOT",                function_jot 		},
	{ "KEY",                function_key 		},
	{ "LASTLOG",		function_lastlog	}, /* lastlog.h */
	{ "LASTSERVER",		function_lastserver	},
	{ "LEFT",		function_left 		},
	{ "LEFTPC",		function_leftpc		},
	{ "LEFTW",              function_leftw 		},
	{ "LINE",		function_line		}, /* lastlog.h */
	{ "LISTARRAY",		function_listarray	},
	{ "LISTEN",		function_listen 	},
	{ "LOADINFO",		function_loadinfo	},
	{ "LOG",		function_log		},
	{ "LOG10",		function_log10		},
	{ "LONGTOIP",		function_longtoip	},
	{ "MASK",		function_mask		},
	{ "MATCH",		function_match 		},
	{ "MATCHITEM",          function_matchitem 	},
	{ "MAXLEN",		function_maxlen		},
	{ "MID",		function_mid 		},
	{ "MIDW",               function_midw 		},
	{ "MKDIR",		function_mkdir		},
	{ "MSAR",		function_msar		},
	{ "MYCHANNELS",		function_channels 	},
	{ "MYSERVERS",		function_servers 	},
	{ "NAMETOIP",		function_nametoip 	},
	{ "NOCHOPS",            function_nochops 	},
	{ "NOHIGHLIGHT",	function_nohighlight	},
	{ "NOTIFY",		function_notify		},
	{ "NOTIFYWINDOWS",	function_notifywindows	},
	{ "NOTW",               function_notw 		},
	{ "NUMARRAYS",          function_numarrays 	},
	{ "NUMITEMS",           function_numitems 	},
	{ "NUMONCHANNEL",	function_numonchannel 	},
	{ "NUMSORT",		function_numsort	},
	{ "NUMWORDS",		function_numwords	},
	{ "ONCHANNEL",          function_onchannel 	},
	{ "OPEN",		function_open 		},
	{ "PAD",		function_pad		},
	{ "PASS",		function_pass		},
	{ "PATTERN",            function_pattern 	},
#ifdef PERL
	{ "PERL",		function_perl		},
	{ "PERLCALL",		function_perlcall	},
	{ "PERLXCALL",		function_perlxcall	},
#endif
	{ "PID",		function_pid 		},
	{ "POP",		function_pop 		},
	{ "PPID",		function_ppid 		},
	{ "PREFIX",		function_prefix		},
	{ "PRINTLEN",		function_printlen	},
	{ "PUSH",		function_push 		},
	{ "QUERYWIN",		function_querywin	},
	{ "RAND",		function_rand 		},
	{ "RANDREAD",		function_randread	},
	{ "READ",		function_read 		},
	{ "REALPATH",		function_realpath	},
	{ "REGCOMP",		function_regcomp	},
	{ "REGERROR",		function_regerror	},
	{ "REGEXEC",		function_regexec	},
	{ "REGFREE",		function_regfree	},
	{ "REGMATCHES",		function_regmatches	},
	{ "REMW",               function_remw 		},
	{ "REMWS",		function_remws		},
	{ "RENAME",		function_rename 	},
	{ "REPEAT",		function_repeat		},
	{ "REST",		function_rest		},
	{ "RESTW",              function_restw 		},
	{ "REVERSE",            function_reverse 	},
	{ "REVW",               function_revw 		},
	{ "RFILTER",            function_rfilter 	},
	{ "RIGHT",		function_right 		},
	{ "RIGHTW",             function_rightw 	},
	{ "RIGMASK",		function_rigmask	},
	{ "RIGTYPE",		function_rigtype	},
	{ "RINDEX",		function_rindex 	},
	{ "RMATCH",		function_rmatch 	},
	{ "RMATCHITEM",         function_rmatchitem 	},
	{ "RMDIR",		function_rmdir 		},
	{ "RPATTERN",           function_rpattern 	},
	{ "RSUBSTR",		function_rsubstr	},
	{ "SAR",		function_sar 		},
	{ "SERVERGROUP",	function_servergroup	},
	{ "SERVERNAME",		function_servername	},
	{ "SERVERNICK",		function_servernick	},
	{ "SERVERNUM",		function_servernum	},
	{ "SERVEROURNAME",	function_serverourname	},
	{ "SERVPORTS",		function_servports	},
	{ "SETITEM",            function_setitem 	},
	{ "SHIFT",		function_shift 		},
	{ "SIN",		function_sin		},
	{ "SINH",		function_sinh		},
	{ "SORT",		function_sort		},
	{ "SPLICE",		function_splice 	},
	{ "SPLIT",		function_split 		},
	{ "SRAND",		function_srand 		},
	{ "SSL",		function_ssl		},
	{ "STAT",		function_stat		},
	{ "STATUS",		function_status		},
	{ "STIME",		function_stime 		},
	{ "STRFTIME",		function_strftime	},
	{ "STRIP",		function_strip 		},
	{ "STRIPANSI",		function_stripansi	},
	{ "STRIPANSICODES",	function_stripansicodes },
	{ "STRIPC",		function_stripc		},
	{ "STRIPCRAP",		function_stripcrap	},
	{ "STRLEN",		function_strlen		},
	{ "SUBSTR",		function_substr		},
	{ "TAN",		function_tan		},
	{ "TANH",		function_tanh		},
	{ "TDIFF",		function_tdiff 		},
	{ "TDIFF2",		function_tdiff2 	},
	{ "TIME",		function_time 		},
	{ "TOLOWER",		function_tolower 	},
	{ "TOUPPER",		function_toupper 	},
	{ "TOW",                function_tow 		},
	{ "TR",			function_translate 	},
	{ "TRUNC",		function_truncate 	},
	{ "TTYNAME",		function_ttyname	},
	{ "TWIDDLE",		function_twiddle	},
	{ "UHC",		function_uhc		}, 
	{ "UMASK",		function_umask		},
	{ "UNAME",		function_uname		},
	{ "UNIQ",		function_uniq		},
	{ "UNLINK",		function_unlink 	},
	{ "UNSHIFT",		function_unshift 	},
	{ "UNSPLIT",		function_unsplit	},
	{ "URLDECODE",		function_urldecode	},
	{ "URLENCODE",		function_urlencode	},
	{ "USERHOST",		function_userhost 	},
	{ "USERMODE",		function_umode		},
	{ "UTIME",		function_utime	 	},
	{ "VERSION",		function_server_version },
	{ "WHICH",		function_which 		},
	{ "WINBOUND",		function_winbound	},
	{ "WINCHAN",		function_winchan	},
	{ "WINCURSORLINE",	function_wincurline	},
	{ "WINLEVEL",		function_winlevel	},
	{ "WINNAM",		function_winnam 	},
	{ "WINNICKLIST",	function_winnames	},
	{ "WINNUM",		function_winnum 	},
	{ "WINREFS",		function_winrefs	},
	{ "WINSCREEN",		function_winscreen	},
	{ "WINSCROLLBACKSIZE",	function_winsbsize	},
	{ "WINSERV",		function_winserv	},
	{ "WINSIZE",		function_winsize	},
	{ "WINSTATUSSIZE",	function_winstatsize	},
	{ "WINVISIBLE",		function_winvisible	},
	{ "WORD",		function_word 		},
	{ "WORDTOINDEX",	function_wordtoindex	},
	{ "WRITE",		function_write 		},
	{ "WRITEB",		function_writeb		},
	{ "YN",			function_yn		},
	{ (char *) 0,		NULL }
};

#define	NUMBER_OF_FUNCTIONS (sizeof(built_in_functions) / sizeof(BuiltInFunctions)) - 2


char	*built_in_alias (char c, int *returnval)
{
	BuiltIns	*tmp;

	for (tmp = built_in;tmp->name;tmp++)
	{
		if (c == tmp->name)
		{
			if (returnval)
			{
				*returnval = 1;
				return NULL;
			}
			else
				return tmp->func();
		}
	}
	return NULL;		/* Pretty sure this should be NULL */
}


/*
 * call_function has changed a little bit.  Now we take the entire call
 * including args in the paren list.  This is a bit more convenient for 
 * the callers, since there are a bunch of them and all of them seperately
 * handling extracting the args is just a pain in the butt.
 */
char	*call_function (char *name, const char *args, int *args_flag)
{
	char	*tmp;
	char	*result = (char *) 0;
	char	*debug_copy = (char *) 0;
	int	cnt, pos;
	char	*lparen, *rparen;
	int	debugging = 0;

	debugging = 1;
	if (get_int_var(DEBUG_VAR) & DEBUG_FUNCTIONS)
		debugging = 2;

	if ((lparen = strchr(name, '(')))
	{
		if ((rparen = MatchingBracket(lparen + 1, '(', ')')))
			*rparen++ = 0;
		else
			yell("Unmatched lparen in function call [%s]", name);

		*lparen++ = 0;
	}
	else
		lparen = empty_string;

	tmp = expand_alias(lparen, args, args_flag, NULL);
	if (debugging)
		debug_copy = LOCAL_COPY(tmp);

	upper(name);
	find_fixed_array_item(built_in_functions, sizeof(BuiltInFunctions), 
				NUMBER_OF_FUNCTIONS + 1, name, &cnt, &pos);

	if (cnt < 0)
		result = built_in_functions[pos].func(tmp);
	else
		result = call_user_function(name, tmp);

	if (debugging)
	{
		char *	buf;
		buf = (char *)alloca(strlen(name) + strlen(debug_copy) + 15);
		sprintf(buf, "$%s(%s)", name, debug_copy);
		MUST_BE_MALLOCED(result, buf);

		if (debugging == 2)
			yell("Function %s(%s) returned %s", 
				name, debug_copy, result);
	}

	new_free(&tmp);
	return result;
}

static int	func_exist (char *name)
{
	int 	cnt, 
		pos;
	char *	tmp;

	tmp = LOCAL_COPY(name);
	upper(tmp);

	find_fixed_array_item(built_in_functions, sizeof(BuiltInFunctions), NUMBER_OF_FUNCTIONS + 1, tmp, &cnt, &pos);
	if (cnt < 0)
		return 1;

	return 0;
}


/* built in expando functions */
static	char	*alias_line 		(void) { return m_strdup(get_input()); }
static	char	*alias_buffer 		(void) { return m_strdup(cut_buffer); }
static	char	*alias_time 		(void) { return m_strdup(update_clock(GET_TIME)); }
static	char	*alias_dollar 		(void) { return m_strdup("$"); }
static	char	*alias_detected 	(void) { return m_strdup(last_notify_nick); }
static	char	*alias_nick 		(void) { return m_strdup((current_window->server != -1) ? get_server_nickname(current_window->server) : empty_string); }
static	char	*alias_away 		(void) { return m_strdup(get_server_away(from_server)); }
static	char	*alias_sent_nick 	(void) { return m_strdup((sent_nick) ? sent_nick : empty_string); }
static	char	*alias_recv_nick 	(void) { return m_strdup((recv_nick) ? recv_nick : empty_string); }
static	char	*alias_msg_body 	(void) { return m_strdup((sent_body) ? sent_body : empty_string); }
static	char	*alias_joined_nick 	(void) { return m_strdup((joined_nick) ? joined_nick : empty_string); }
static	char	*alias_public_nick 	(void) { return m_strdup((public_nick) ? public_nick : empty_string); }
static  char    *alias_show_realname 	(void) { return m_strdup(realname); }
static	char	*alias_version_str 	(void) { return m_strdup(irc_version); }
static	char	*alias_invite 		(void) { return m_strdup((invite_channel) ? invite_channel : empty_string); }
static	char	*alias_oper 		(void) { return m_strdup((from_server != -1) ? get_server_operator(from_server) ?  get_string_var(STATUS_OPER_VAR) : empty_string : empty_string); }
static	char	*alias_version 		(void) { return m_strdup(internal_version); }
static  char    *alias_show_userhost 	(void) { return m_strdup(get_server_userhost(from_server)); }
static  char    *alias_online 		(void) { return m_sprintf("%ld",(long)start_time.tv_sec); }
static  char    *alias_idle 		(void) { return m_sprintf("%ld",time(NULL)-idle_time.tv_sec); }
static	char	*alias_current_numeric	(void) { return m_sprintf("%03d", -current_numeric); }
static	char	*alias_banner		(void) { return m_strdup(numeric_banner()); }

static	char	*alias_currdir  	(void)
{
	char 	*tmp = (char *)new_malloc(MAXPATHLEN+1);
	return getcwd(tmp, MAXPATHLEN);
}

static	char	*alias_channel 		(void) 
{ 
	char	*tmp; 
	return m_strdup((tmp = get_channel_by_refnum(0)) ? tmp : zero);
}

static	char	*alias_server 		(void)
{
	return m_strdup((parsing_server_index != -1) ?
		         get_server_itsname(parsing_server_index) :
		         (get_window_server(0) != -1) ?
			        get_server_itsname(get_window_server(0)) : empty_string);
}

static	char	*alias_query_nick 	(void)
{
	const char	*tmp;
	return m_strdup((tmp = query_nick()) ? tmp : empty_string);
}

static	char	*alias_target 		(void)
{
	char	*tmp;
	return m_strdup((tmp = get_target_by_refnum(0)) ? tmp : empty_string);
}

static	char	*alias_cmdchar 		(void)
{
	char	*cmdchars, tmp[2];

	if ((cmdchars = get_string_var(CMDCHARS_VAR)) == (char *) 0)
		cmdchars = DEFAULT_CMDCHARS;
	tmp[0] = cmdchars[0];
	tmp[1] = 0;
	return m_strdup(tmp);
}

static	char	*alias_chanop 		(void)
{
	char	*tmp;
	return m_strdup(((tmp = get_channel_by_refnum(0)) && get_channel_oper(tmp, from_server)) ?
		"@" : empty_string);
}

static	char	*alias_modes 		(void)
{
	char	*tmp;
	return m_strdup((tmp = get_channel_by_refnum(0)) ?
		get_channel_mode(tmp, from_server) : empty_string);
}

static	char	*alias_server_version  (void)
{
	int s = from_server;

	if (s == -1)
	{
		if (primary_server != -1)
			s = primary_server;
		else
			return m_strdup(empty_string);
	}

	return m_strdup(get_server_version_string(s));
}


/*	*	*	*	*	*	*	*	*	*
		These are the built-in functions.

	About 80 of them are here, the rest are in array.c.  All of the
	stock client's functions are supported, as well as about 60 more.
	Most of the 30 stock client's functions have been re-written for
	optimization reasons, and also to further distance ircii's code
	from EPIC.
 *	*	*	*	*	*	*	*	*	*/

/* 
 * These are defined to make the construction of the built-in functions
 * easier and less prone to bugs and unexpected behaviors.  As long as
 * you consistently use these macros to do the dirty work for you, you
 * will never have to do bounds checking as the macros do that for you. >;-) 
 *
 * Yes, i realize it makes the code slightly less efficient, but i feel that 
 * the cost is minimal compared to how much time i have spent over the last 
 * year debugging these functions and the fact i wont have to again. ;-)
 */
#define EMPTY empty_string
#define EMPTY_STRING m_strdup(EMPTY)
#define RETURN_EMPTY return EMPTY_STRING
#define RETURN_IF_EMPTY(x) if (empty( (x) )) RETURN_EMPTY
#define GET_INT_ARG(x, y) {RETURN_IF_EMPTY((y)); x = my_atol(safe_new_next_arg((y), &(y)));}
#define GET_FLOAT_ARG(x, y) {RETURN_IF_EMPTY((y)); x = atof(safe_new_next_arg((y), &(y)));}
#define GET_STR_ARG(x, y) {RETURN_IF_EMPTY((y)); x = new_next_arg((y), &(y));RETURN_IF_EMPTY((x));}
#define RETURN_MSTR(x) return ((x) ? (x) : EMPTY_STRING);
#define RETURN_STR(x) return m_strdup((x) ? (x) : EMPTY)
#define RETURN_INT(x) return m_strdup(ltoa((x)))
#define RETURN_FLOAT(x) return m_sprintf("%.50g", (double) (x))

/*
 * XXXX REALLY REALLY REALLY REALLY REALLY REALLY REALLY IMPORTANT! XXXX
 *
 * Don't ever Ever EVER pass a function call to the RETURN_* macros.
 * They _WILL_ evaluate the term twice, and for some function calls, 
 * that can result in a memory leak, or worse.
 */

#define BUILT_IN_FUNCTION(x, y) static char * x (char * y)

/*
 * Usage: $left(number text)
 * Returns: the <number> leftmost characters in <text>.
 * Example: $left(5 the quick brown frog) returns "the q"
 *
 * Note: the difference between $[10]foo and $left(10 foo) is that the former
 * is padded and the latter is not.
 */
BUILT_IN_FUNCTION(function_left, word)
{
	int	count;

	GET_INT_ARG(count, word);
	RETURN_IF_EMPTY(word);

	if (count < 0)
		RETURN_EMPTY;

	if (strlen(word) > count)
		word[count] = 0;

	RETURN_STR(word);
}

/*
 * Usage: $right(number text)
 * Returns: the <number> rightmost characters in <text>.
 * Example: $right(5 the quick brown frog) returns " frog"
 */
BUILT_IN_FUNCTION(function_right, word)
{
	int	count;

	GET_INT_ARG(count, word);
	RETURN_IF_EMPTY(word);

	if (count < 0)
		RETURN_EMPTY;

	if (strlen(word) > count)
		word += strlen(word) - count;

	RETURN_STR(word);
}

/*
 * Usage: $mid(start number text)
 * Returns: the <start>th through <start>+<number>th characters in <text>.
 * Example: $mid(3 4 the quick brown frog) returns " qui"
 *
 * Note: the first character is numbered zero.
 */
BUILT_IN_FUNCTION(function_mid, word)
{
	int	start, length;

	GET_INT_ARG(start, word);
	GET_INT_ARG(length, word);
	RETURN_IF_EMPTY(word);

	if (start < strlen(word))
	{
		word += start;
		if (length < 0)
			RETURN_EMPTY;

		if (length < strlen(word))
			word[length] = 0;
	}
	else
		word = EMPTY;

	RETURN_STR(word);
}


/*
 * Usage: $rand(max)
 * Returns: A random number from zero to max-1.
 * Example: $rand(10) might return any number from 0 to 9.
 */
BUILT_IN_FUNCTION(function_rand, word)
{
	long	tempin;

	GET_INT_ARG(tempin, word);
	if (tempin == 0)
		tempin = (unsigned long) -1;	/* This is cheating. :P */
	RETURN_INT(random_number(0) % tempin);
}

/*
 * Usage: $srand(seed)
 * Returns: Nothing.
 * Side effect: seeds the random number generater.
 * Note: the argument is ignored.
 */
BUILT_IN_FUNCTION(function_srand, word)
{
	random_number(time(NULL));
	RETURN_EMPTY;
}

/*
 * Usage: $time()
 * Returns: The number of seconds that has elapsed since Jan 1, 1970, GMT.
 * Example: $time() returned something around 802835348 at the time I
 * 	    wrote this comment.
 */
BUILT_IN_FUNCTION(function_time, input)
{
	RETURN_INT(time(NULL));
}

/*
 * Usage: $stime(time)
 * Returns: The human-readable form of the date based on the <time> argument.
 * Example: $stime(1000) returns what time it was 1000 seconds from the epoch.
 * 
 * Note: $stime() is really useful when you give it the argument $time(), ala
 *       $stime($time()) is the human readable form for now.
 */
BUILT_IN_FUNCTION(function_stime, input)
{
	time_t	ltime;
	char	*ret;

	GET_INT_ARG(ltime, input);
	ret = my_ctime(ltime);
	RETURN_STR(ret);		/* Dont put function call in macro! */
}

/*
 * Usage: $tdiff(seconds)
 * Returns: The time that has elapsed represented in days/hours/minutes/seconds
 *          corresponding to the number of seconds passed as the argument.
 * Example: $tdiff(3663) returns "1 hour 1 minute 3 seconds"
 */
BUILT_IN_FUNCTION(function_tdiff, input)
{
	time_t	ltime;
	time_t	days,
		hours,
		minutes,
		seconds;
	size_t	size;
	char	*tmp;
	char	*after;

	size = strlen(input) + 64;
	tmp = alloca(size);
	*tmp = 0;

	ltime = (time_t)strtol(input, &after, 10);
	if (after == input)
		RETURN_EMPTY;

	seconds = ltime % 60;
	ltime /= 60;
	minutes = ltime % 60;
	ltime /= 60;
	hours = ltime % 24;
	days = (ltime - hours) / 24;

	if (days)
	{
		if (days == 1)
			strmcat(tmp, "1 day ", size);
		else
			strmpcat(tmp, size, "%ld days ", (long)days);
	}
	if (hours)
	{
		if (hours == 1)
			strmcat(tmp, "1 hour ", size);
		else
			strmpcat(tmp, size, "%ld hours ", (long)hours);
	}
	if (minutes)
	{
		if (minutes == 1)
			strmcat(tmp, "1 minute ", size);
		else
			strmpcat(tmp, size, "%ld minutes ", (long)minutes);
	}

	if (seconds || (!days && !hours && !minutes) || 
			(*after == '.' && is_number(after + 1)))
	{
		u_long number = 0;

		/*
		 * If we have a decmial point, and is_number() returns 1,
		 * then we know that we have a real, authentic number AFTER
		 * the decmial point.  As long as it isnt zero, we want it.
		 */
		strmcat(tmp, ltoa(seconds), size);
		if (*after == '.')
		{
			if ((number = atol(after + 1)))
				strmcat(tmp, after, size);
		}

		if (seconds == 1 && number == 0)
			strmcat(tmp, " second", size);
		else
			strmcat(tmp, " seconds", size);
	}
	else
		chop(tmp, 1);	/* Chop off that space! */

	RETURN_STR(tmp);
}

/*
 * Usage: $index(characters text)
 * Returns: The number of leading characters in <text> that do not occur 
 *          anywhere in the <characters> argument.
 * Example: $index(f three fine frogs) returns 6 (the 'f' in 'fine')
 *          $index(frg three fine frogs) returns 2 (the 'r' in 'three')
 */
BUILT_IN_FUNCTION(function_index, input)
{
	char	*schars;
	char	*iloc;

	GET_STR_ARG(schars, input);
	iloc = sindex(input, schars);
	RETURN_INT(iloc ? iloc - input : -1);
}

/*
 * Usage: $rindex(characters text)
 * Returns: The number of leading characters in <text> that occur before the
 *          *last* occurance of any of the characters in the <characters> 
 *          argument.
 * Example: $rindex(f three fine frogs) returns 12 (the 'f' in 'frogs')
 *          $rindex(frg three fine frogs) returns 15 (the 'g' in 'frogs')
 */
BUILT_IN_FUNCTION(function_rindex, word)
{
	char	*chars, *last;

	/* need to find out why ^x doesnt work */
	GET_STR_ARG(chars, word);
	if (!*word || !*chars)
		RETURN_INT(-1);

	last = rsindex(word + strlen(word) - 1, word, chars, 1);
	RETURN_INT(last ? last - word : -1);
}

/*
 * Usage: $match(pattern list of words)
 * Returns: if no words in the list match the pattern, it returns 0.
 *	    Otherwise, it returns the number of the word that most
 *	    exactly matches the pattern (first word is numbered one)
 * Example: $match(f*bar foofum barfoo foobar) returns 3
 *	    $match(g*ant foofum barfoo foobar) returns 0
 *
 * Note: it is possible to embed spaces inside of a word or pattern simply
 *       by including the entire word or pattern in quotation marks. (")
 */
BUILT_IN_FUNCTION(function_match, input)
{
	char	*pattern, 	*word;
	long	current_match,	best_match = 0,	match = 0, match_index = 0;

	GET_STR_ARG(pattern, input);

	while (input && *input)
	{
		while (input && my_isspace(*input))
			input++;
		match_index++;
		GET_STR_ARG(word, input);
		if ((current_match = wild_match(pattern, word)) > best_match)
		{
			match = match_index;
			best_match = current_match;
		}
	}

	RETURN_INT(match);
}

/*
 * Usage: $rmatch(word list of patterns)
 * Returns: if no pattern in the list matches the word, it returns 0.
 *	    Otherwise, it returns the number of the pattern that most
 *	    exactly matches the word (first word is numbered one)
 * Example: $rmatch(foobar f*bar foo*ar g*ant) returns 2 
 *	    $rmatch(booya f*bar foo*ar g*ant) returns 0
 * 
 * Note: It is possible to embed spaces into a word or pattern simply by
 *       including the entire word or pattern within quotation marks (")
 */
BUILT_IN_FUNCTION(function_rmatch, input)
{
	char	*pattern,	*word;
	int	current_match,	best_match = 0,	match = 0, rmatch_index = 0;

	GET_STR_ARG(word, input);

	while (input && *input)
	{
		while (input && my_isspace(*input))
			input++;
		rmatch_index++;
		GET_STR_ARG(pattern, input);
		if ((current_match = wild_match(pattern, word)) > best_match)
		{
			match = rmatch_index;
			best_match = current_match;
		}
		/* WARNING WARNING HACK IN PROGRESS WARNING WARNING */
		while (input && my_isspace(*input))
			input++;
	}

	RETURN_INT(match);
}

/*
 * Usage: $userhost()
 * Returns: the userhost (if any) of the most previously recieved message.
 * Caveat: $userhost() changes with every single line that appears on
 *         your screen, so if you want to save it, you will need to assign
 *         it to a variable.
 */
BUILT_IN_FUNCTION(function_userhost, input)
{
	if (input && *input)
	{
		char *retval = NULL;
		size_t rvclue=0;
		char *nick;
		const char *userhost;

		while (input && *input)
		{
			GET_STR_ARG(nick, input);
			if ((userhost = fetch_userhost(from_server, nick)))
				m_sc3cat(&retval, space, userhost, &rvclue);
			else
				m_sc3cat(&retval, space, unknown_userhost, &rvclue);
		}
		RETURN_MSTR(retval);
	}

	RETURN_STR(FromUserHost);
}

/* 
 * Usage: $strip(characters text)
 * Returns: <text> with all instances of any characters in the <characters>
 *	    argument removed.
 * Example: $strip(f free fine frogs) returns "ree ine rogs"
 *
 * Note: it can be difficult (actually, not possible) to remove spaces from
 *       a string using this function.  To remove spaces, simply use this:
 *		$tr(/ //$text)
 *
 *	 Actually, i recommend not using $strip() at all and just using
 *		$tr(/characters//$text)
 *	 (but then again, im biased. >;-)
 */
BUILT_IN_FUNCTION(function_strip, input)
{
	char	*result;
	char	*chars;
	char	*cp, *dp;

	GET_STR_ARG(chars, input);
	RETURN_IF_EMPTY(input);

	result = (char *)new_malloc(strlen(input) + 1);
	for (cp = input, dp = result; *cp; cp++)
	{
		/* This is expensive -- gotta be a better way */
		if (!strchr(chars, *cp))
			*dp++ = *cp;
	}
	*dp = '\0';

	return result;		/* DONT USE RETURN_STR HERE! */
}

/*
 * Usage: $encode(text)
 * Returns: a string, uniquely identified with <text> such that the string
 *          can be used as a variable name.
 * Example: $encode(fe fi fo fum) returns "GGGFCAGGGJCAGGGPCAGGHFGN"
 *
 * Note: $encode($decode(text)) returns text (most of the time)
 *       $decode($encode(text)) also returns text.
 */
static char	*function_encode (unsigned char *input)
{
	return encode(input, strlen(input));	/* DONT USE RETURN_STR HERE! */
}


/*
 * Usage: $decode(text)
 * Returns: If <text> was generated with $encode(), it returns the string
 *          you originally encoded.  If it wasnt, you will probably get
 *	    nothing useful in particular.
 * Example: $decode(GGGFCAGGGJCAGGGPCAGGHFGN) returns "fe fi fo fum"
 *
 * Note: $encode($decode(text)) returns "text"
 *       $decode($encode(text)) returns "text" too.
 *
 * Note: Yes.  $decode(plain-text) can compress the data in half, but it is
 *	 lossy compression (more than one plain-text can yeild identical
 *	 output), so most input is irreversably mangled.  Dont do it.
 */
static char	*function_decode (unsigned char *input)
{
	return decode(input);	/* DONT USE RETURN_STR HERE! */
}


/*
 * Usage: $ischannel(text)
 * Returns: If <text> could be a valid channel name, 1 is returned.
 *          If <text> is an invalid channel name, 0 is returned.
 *
 * Note: Contrary to popular belief, this function does NOT determine
 * whether a given channel name is in use!
 */
BUILT_IN_FUNCTION(function_ischannel, input)
{
	RETURN_INT(is_channel(input));
}

/*
 * Usage: $ischanop(nick channel)
 * Returns: 1 if <nick> is a channel operator on <channel>
 *          0 if <nick> is not a channel operator on <channel>
 *			* O R *
 *	      if you are not on <channel>
 *
 * Note: Contrary to popular belief, this function can only tell you
 *       who the channel operators are for channels you are already on!
 *
 * Boo Hiss:  This should be $ischanop(channel nick <nick...nick>)
 *	      and return a list (1 1 ... 0), which would allow us to
 *	      call is_chanop() without ripping off the nick, and allow 
 *	      us to abstract is_chanop() to take a list. oh well... 
 *	      Too late to change it now. :/
 */
BUILT_IN_FUNCTION(function_ischanop, input)
{
	char	*nick;

	GET_STR_ARG(nick, input);
	RETURN_INT(is_chanop(input, nick));
}


/*
 * Usage: $word(number text)
 * Returns: the <number>th word in <text>.  The first word is numbered zero.
 * Example: $word(3 one two three four five) returns "four" (think about it)
 */
BUILT_IN_FUNCTION(function_word, word)
{
	int	cvalue;
	char	*w_word;

	GET_INT_ARG(cvalue, word);
	if (cvalue < 0)
		RETURN_EMPTY;

	while (cvalue-- > 0)
		GET_STR_ARG(w_word, word);

	GET_STR_ARG(w_word, word);
	RETURN_STR(w_word);
}


/*
 * Usage: $winnum()
 * Returns: the index number for the current window
 * 
 * Note: returns -1 if there are no windows open (ie, in dumb mode)
 */
BUILT_IN_FUNCTION(function_winnum, input)
{
	Window *win = NULL;
	if (!(win = *input ? get_window_by_desc(input) : current_window))
		RETURN_INT(-1);
	RETURN_INT(win->refnum);
}

BUILT_IN_FUNCTION(function_winnam, input)
{
	Window *win = NULL;
	if (!(win = *input ? get_window_by_desc(input) : current_window))
		RETURN_EMPTY;
	RETURN_STR(win->name);
}

BUILT_IN_FUNCTION(function_connect, input)
{
	char	*host;
	int	port;

	GET_STR_ARG(host, input);
	GET_INT_ARG(port, input);

	return dcc_raw_connect(host, port);	/* DONT USE RETURN_STR HERE! */
}


BUILT_IN_FUNCTION(function_listen, input)
{
	int	port = 0;

	/* Oops. found by CrowMan, listen() has a default. erf. */
	if (input && *input)
	{
		char *tmp, *ptr;
		if ((tmp = new_next_arg(input, &input)))
		{
			port = strtoul(tmp, &ptr, 0);
			if (ptr == tmp)
				RETURN_EMPTY;	/* error. */
		}
	}

	return dcc_raw_listen(port);		/* DONT USE RETURN_STR HERE! */
}

BUILT_IN_FUNCTION(function_toupper, input)
{
	return (upper(m_strdup(input)));
}

BUILT_IN_FUNCTION(function_tolower, input)
{
	return (lower(m_strdup(input)));
}

BUILT_IN_FUNCTION(function_curpos, input)
{
	RETURN_INT(current_window->screen->buffer_pos);
}

BUILT_IN_FUNCTION(function_channels, input)
{
	int	server = from_server;
	char *	retval;

	if (isdigit(*input)) 
		GET_INT_ARG(server, input)
	else if (*input)
	{
		Window  *window;

		server = -1;
 		if ((window = get_window_by_desc(input)))
			server = window->server;
	}

	retval = create_channel_list(server);
	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_servers, input)
{
	int count;
	char *retval = NULL;
	size_t rvclue=0;

	if (!input || !*input)
	{
		retval = create_server_list();
		RETURN_MSTR(retval);
	}

	for (count = 0; count < number_of_servers; count++)
	{
		if (is_server_connected(count))
			m_sc3cat(&retval, space, ltoa(count), &rvclue);
	}
	if (!retval)
		RETURN_EMPTY;

	return retval;
}

BUILT_IN_FUNCTION(function_pid, input)
{
	RETURN_INT(getpid());
}

BUILT_IN_FUNCTION(function_ppid, input)
{
	RETURN_INT(getppid());
}


/*
 * strftime() patch from hari (markc@arbld.unimelb.edu.au)
 */
BUILT_IN_FUNCTION(function_strftime, input)
{
	char		result[128];
	time_t		ltime;
	struct tm	*tm;

	if (isdigit(*input))
		ltime = strtoul(input, &input, 0);
	else
		ltime = time(NULL);

	while (*input && my_isspace(*input))
		++input; 

	if (!*input)
		return m_strdup(empty_string);


	tm = localtime(&ltime);

	if (!strftime(result, 128, input, tm))
		return m_strdup(empty_string);

	return m_strdup(result);
}

BUILT_IN_FUNCTION(function_idle, input)
{
	return alias_idle();
}



/* The new "added" functions */

/* $before(chars string of text)
 * returns the part of "string of text" that occurs before the
 * first instance of any character in "chars"
 * EX:  $before(! hop!jnelson@iastate.edu) returns "hop"
 */
BUILT_IN_FUNCTION(function_before, word)
{
	char	*pointer = (char *) 0;
	char	*chars;
	char	*tmp;
	long	numint;

	GET_STR_ARG(tmp, word);			/* DONT DELETE TMP! */
	numint = my_atol(tmp);

	if (numint)
	{
		GET_STR_ARG(chars, word);
	}
	else
	{
		numint = 1;
		chars = tmp;
	}

	if (numint < 0 && strlen(word))
		pointer = word + strlen(word) - 1;

	pointer = search(word, &pointer, chars, numint);

	if (!pointer)
		RETURN_EMPTY;

	*pointer = '\0';
	RETURN_STR(word);
}

/* $after(chars string of text)
 * returns the part of "string of text" that occurs after the 
 * first instance of any character in "chars"
 * EX: $after(! hop!jnelson@iastate.edu)  returns "jnelson@iastate.edu"
 */
BUILT_IN_FUNCTION(function_after, word)
{
	char	*chars;
	char	*pointer = (char *) 0;
	char 	*tmp;
	long	numint;

	GET_STR_ARG(tmp, word);
	numint = my_atol(tmp);

	if (numint)
		chars = new_next_arg(word, &word);
	else
	{
		numint = 1;
		chars = tmp;
	}

	if (numint < 0 && strlen(word))
		pointer = word + strlen(word) - 1;

	pointer = search(word, &pointer, chars, numint);

	if (!pointer || !*pointer)
		RETURN_EMPTY;

	RETURN_STR(pointer + 1);
}

/* $leftw(num string of text)
 * returns the left "num" words in "string of text"
 * EX: $leftw(3 now is the time for) returns "now is the"
 */
BUILT_IN_FUNCTION(function_leftw, word)
{
	int value;
 
	GET_INT_ARG(value, word);
	if (value < 1)
		RETURN_EMPTY;

	return (extractw(word, 0, value-1));	/* DONT USE RETURN_STR HERE! */
}

/* $rightw(num string of text)
 * returns the right num words in "string of text"
 * EX: $rightw(3 now is the time for) returns "the time for"
 */
BUILT_IN_FUNCTION(function_rightw, word)
{
	int     value;

	GET_INT_ARG(value, word);
	if (value < 1)
		RETURN_EMPTY;
		
	return extractw2(word, -value, EOS); 
}


/* $midw(start num string of text)
 * returns "num" words starting at word "start" in the string "string of text"
 * NOTE: The first word is word #0.
 * EX: $midw(2 2 now is the time for) returns "the time"
 */
BUILT_IN_FUNCTION(function_midw, word)
{
	int     start, num;

	GET_INT_ARG(start, word);
	GET_INT_ARG(num, word);

	if (num < 1)
		RETURN_EMPTY;

	return extractw(word, start, (start + num - 1));
}

/* $notw(num string of text)
 * returns "string of text" with word number "num" removed.
 * NOTE: The first word is numbered 0.
 * EX: $notw(3 now is the time for) returns "now is the for"
 */
BUILT_IN_FUNCTION(function_notw, word)
{
	char    *booya = (char *)0;
	int     where;
	
	GET_INT_ARG(where, word);

	/* An invalid word simply returns the string as-is */
	if (where < 0)
		RETURN_STR(word);

	if (where > 0)
	{
		char *part1, *part2;
		part1 = extractw(word, 0, (where - 1));
		part2 = extractw(word, (where + 1), EOS);
		booya = m_strdup(part1);
		/* if part2 is there, append it. */
		m_s3cat_s(&booya, space, part2);
		new_free(&part1);
		new_free(&part2);
	}
	else /* where == 0 */
		booya = extractw(word, 1, EOS);

	return booya;				/* DONT USE RETURN_STR HERE! */
}

/* 
 * $restw(num string of text)
 * returns "string of text" that occurs starting with and including
 * word number "num"
 * NOTE: the first word is numbered 0.
 * EX: $restw(3 now is the time for) returns "time for"
 */
BUILT_IN_FUNCTION(function_restw, word)
{
	int     where;
	
	GET_INT_ARG(where, word);
	if (where < 0)
		RETURN_EMPTY;
	return extractw(word, where, EOS);
}

/* 
 * $remw(word string of text)
 * returns "string of text" with the word "word" removed
 * EX: $remw(the now is the time for) returns "now is time for"
 */
BUILT_IN_FUNCTION(function_remw, word)
{
	char 	*word_to_remove;
	int	len;
	char	*str;

	GET_STR_ARG(word_to_remove, word);
	len = strlen(word_to_remove);

	str = stristr(word, word_to_remove);
	for (; str && *str; str = stristr(str + 1, word_to_remove))
	{
		if (str == word || isspace(str[-1]))
		{
			if (!str[len] || isspace(str[len]))
			{
				if (!str[len])
				{
					if (str != word)
						str--;
					*str = 0;
				}
				else if (str > word)
				{
					char *safe;
					/* Obviously this is safe. */
					safe = LOCAL_COPY(str + len);
					strcpy(str - 1, safe);
				}
				else 
				{
					char *safe;
					/* Obviously this is safe. */
					safe = LOCAL_COPY(str + len + 1);
					strcpy(str, safe);
				}
				break;
			}
		}
	}

	RETURN_STR(word);
}

/* 
 * $insertw(num word string of text)
 * returns "string of text" such that "word" is the "num"th word
 * in the string.
 * NOTE: the first word is numbered 0.
 * EX: $insertw(3 foo now is the time for) returns "now is the foo time for"
 */
BUILT_IN_FUNCTION(function_insertw, word)
{
	int     where;
	char    *what;
	char    *booya=(char *)0;
	char 	*str1, *str2;

	GET_INT_ARG(where, word);
	
	/* If the word goes at the front of the string, then it
	   already is: return it. ;-) */
	if (where < 1)
		booya = m_strdup(word);
	else
	{
		GET_STR_ARG(what, word);
		str1 = extractw(word, 0, (where - 1));
		str2 = extractw(word, where, EOS);
		if (str1 && *str1)
			booya = m_2dup(str1, space);
		malloc_strcat(&booya, what);
		m_s3cat_s(&booya, space, str2);
		new_free(&str1);
		new_free(&str2);
	}

	return booya;				/* DONT USE RETURN_STR HERE! */
}

/* $chngw(num word string of text)
 * returns "string of text" such that the "num"th word is removed 
 * and replaced by "word"
 * NOTE: the first word is numbered 0
 * EX: $chngw(3 foo now is the time for) returns "now is the foo for"
 */
BUILT_IN_FUNCTION(function_chngw, word)
{
	int     which;
	char    *what;
	char    *booya=(char *)0;
	char	*str1, *str2;
	
	GET_INT_ARG(which, word);
	GET_STR_ARG(what, word);

	if (which < 0)
		RETURN_STR(word);

	/* hmmm. if which is 0, extract does the wrong thing. */
	str1 = extractw(word, 0, which - 1);
	str2 = extractw(word, which + 1, EOS);
	if (str1 && *str1)
		booya = m_2dup(str1, space);
	malloc_strcat(&booya, what);
	m_s3cat_s(&booya, space, str2);
	new_free(&str1);
	new_free(&str2);

	return (booya);
}


/* $common (string of text / string of text)
 * Given two sets of words seperated by a forward-slash '/', returns
 * all words that are found in both sets.
 * EX: $common(one two three / buckle my two shoe one) returns "one two"
 * NOTE: returned in order found in first string.
 * NOTE: This may fudge if you have a word in the first set for which
 *       there is a word in the second set that is a superset of the word:
 *	 $common(one two three / phone ooga booga) returns "one"
 */
BUILT_IN_FUNCTION(function_common, word)
{
	char    *left = (char *) 0, **leftw = NULL,
		*right = (char *) 0, **rightw = NULL, *booya = NULL;
	int	leftc, lefti,
		rightc, righti;
	size_t	rvclue=0;

	left = word;
	if (!(right = strchr(word,'/')))
		RETURN_EMPTY;

	*right++ = 0;
	leftc = splitw(left, &leftw);
	rightc = splitw(right, &rightw);

	for (lefti = 0; lefti < leftc; lefti++)
	{
		for (righti = 0; righti < rightc; righti++)
		{
			if (rightw[righti] && !my_stricmp(leftw[lefti], rightw[righti]))
			{
				m_sc3cat(&booya, space, leftw[lefti], &rvclue);
				rightw[righti] = NULL;
			}
		}
	}

	new_free((char **)&leftw);
	new_free((char **)&rightw);

	RETURN_MSTR(booya);
}

/* 
 * $diff(string of text / string of text)
 * given two sets of words, seperated by a forward-slash '/', returns
 * all words that are not found in both sets
 * EX: $diff(one two three / buckle my two shoe)
 * returns "one three buckle my shoe"
 */
BUILT_IN_FUNCTION(function_diff, word)
{
	char 	*left = NULL, **leftw = NULL,
	     	*right = NULL, **rightw = NULL, *booya = NULL;
	int 	lefti, leftc,
	    	righti, rightc;
	int 	found;
	size_t	rvclue=0;

	left = word;
	if ((right = strchr(word, '/')) == (char *) 0)
		RETURN_EMPTY;

	*right++ = 0;
	leftc = splitw(left, &leftw);
	rightc = splitw(right, &rightw);

	for (rvclue = lefti = 0; lefti < leftc; lefti++)
	{
		found = 0;
		for (righti = 0; righti < rightc; righti++)
		{
			if (rightw[righti] && !my_stricmp(leftw[lefti], rightw[righti]))
			{
				found = 1;
				rightw[righti] = NULL;
			}
		}
		if (!found)
			m_sc3cat(&booya, space, leftw[lefti], &rvclue);
	}

	for (rvclue = righti = 0; righti < rightc; righti++)
	{
		if (rightw[righti])
			m_sc3cat(&booya, space, rightw[righti], &rvclue);
	}

	new_free((char **)&leftw);
	new_free((char **)&rightw);

	if (!booya)
		RETURN_EMPTY;

	return (booya);
}

/* $pattern(pattern string of words)
 * given a pattern and a string of words, returns all words that
 * are matched by the pattern
 * EX: $pattern(f* one two three four five) returns "four five"
 */
BUILT_IN_FUNCTION(function_pattern, word)
{
	char    *blah;
	char    *booya = NULL;
	char    *pattern;
	size_t	rvclue=0;

	GET_STR_ARG(pattern, word)
	while (((blah = new_next_arg(word, &word)) != NULL))
	{
		if (wild_match(pattern, blah))
			m_sc3cat(&booya, space, blah, &rvclue);
	}
	RETURN_MSTR(booya);
}

/* $filter(pattern string of words)
 * given a pattern and a string of words, returns all words that are 
 * NOT matched by the pattern
 * $filter(f* one two three four five) returns "one two three"
 */
BUILT_IN_FUNCTION(function_filter, word)
{
	char    *blah;
	char    *booya = NULL;
	char    *pattern;
	size_t	rvclue=0;

	GET_STR_ARG(pattern, word)
	while ((blah = new_next_arg(word, &word)) != NULL)
	{
		if (!wild_match(pattern, blah))
			m_sc3cat(&booya, space, blah, &rvclue);
	}
	RETURN_MSTR(booya);
}

/* $rpattern(word list of patterns)
 * Given a word and a list of patterns, return all patterns that
 * match the word.
 * EX: $rpattern(jnelson@iastate.edu *@* jnelson@* f*@*.edu)
 * returns "*@* jnelson@*"
 */
BUILT_IN_FUNCTION(function_rpattern, word)
{
	char    *blah;
	char    *booya = NULL;
	char    *pattern;
	size_t	rvclue=0;

	GET_STR_ARG(blah, word)

	while ((pattern = new_next_arg(word, &word)) != NULL)
	{
		if (wild_match(pattern, blah))
			m_sc3cat(&booya, space, pattern, &rvclue);
	}
	RETURN_MSTR(booya);
}

/* $rfilter(word list of patterns)
 * given a word and a list of patterns, return all patterns that
 * do NOT match the word
 * EX: $rfilter(jnelson@iastate.edu *@* jnelson@* f*@*.edu)
 * returns "f*@*.edu"
 */
BUILT_IN_FUNCTION(function_rfilter, word)
{
	char    *blah;
	char    *booya = NULL;
	char    *pattern;
	size_t	rvclue=0;

	GET_STR_ARG(blah, word)
	while ((pattern = new_next_arg(word, &word)) != NULL)
	{
		if (!wild_match(pattern, blah))
			m_sc3cat(&booya, space, pattern, &rvclue);
	}
	RETURN_MSTR(booya);
}

/* $copattern(pattern var_1 var_2)
 * Given a pattern and two variable names, it returns all words
 * in the variable_2 corresponding to any words in variable_1 that
 * are matched by the pattern
 * EX: @nicks = [hop IRSMan skip]
 *     @userh = [jnelson@iastate.edu irsman@iastate.edu sanders@rush.cc.edu]
 *     $copattern(*@iastate.edu userh nicks) 
 *	returns "hop IRSMan"
 */
BUILT_IN_FUNCTION(function_copattern, word)
{
	char	*booya = (char *) 0,
		*pattern = (char *) 0,
		*firstl = (char *) 0, *firstlist = (char *) 0, *firstel = (char *) 0,
		*secondl = (char *) 0, *secondlist = (char *) 0, *secondel = (char *) 0;
	char 	*sfirstl, *ssecondl;
	size_t	rvclue=0;

	GET_STR_ARG(pattern, word);
	GET_STR_ARG(firstlist, word);
	GET_STR_ARG(secondlist, word);

	firstl = get_variable(firstlist);
	secondl = get_variable(secondlist);
	sfirstl = firstl;
	ssecondl = secondl;

	while ((firstel = new_next_arg(firstl, &firstl)))
	{
		if (!(secondel = new_next_arg(secondl, &secondl)))
			break;

		if (wild_match(pattern, firstel))
			m_sc3cat(&booya, space, secondel, &rvclue);
	}
	new_free(&sfirstl);
	new_free(&ssecondl);
	RETURN_MSTR(booya);
}

/* $corpattern(pattern var_1 var_2)
 *
 * As per $copattern(), except that the strings in var_1 are wildcard patterns.
 */
BUILT_IN_FUNCTION(function_corpattern, word)
{
	char	*booya = (char *) 0,
		*pattern = (char *) 0,
		*firstl = (char *) 0, *firstlist = (char *) 0, *firstel = (char *) 0,
		*secondl = (char *) 0, *secondlist = (char *) 0, *secondel = (char *) 0;
	char 	*sfirstl, *ssecondl;
	size_t	rvclue=0;

	GET_STR_ARG(pattern, word);
	GET_STR_ARG(firstlist, word);
	GET_STR_ARG(secondlist, word);

	firstl = get_variable(firstlist);
	secondl = get_variable(secondlist);
	sfirstl = firstl;
	ssecondl = secondl;

	while ((firstel = new_next_arg(firstl, &firstl)))
	{
		if (!(secondel = new_next_arg(secondl, &secondl)))
			break;

		if (wild_match(firstel, pattern))
			m_sc3cat(&booya, space, secondel, &rvclue);
	}
	new_free(&sfirstl);
	new_free(&ssecondl);
	RETURN_MSTR(booya);
}

/* $beforew(pattern string of words)
 * returns the portion of "string of words" that occurs before the 
 * first word that is matched by "pattern"
 * EX: $beforew(three one two three o leary) returns "one two"
 */
BUILT_IN_FUNCTION(function_beforew, word)
{
	int     where;
	char	*lame = (char *) 0;
	char	*placeholder;

	lame = LOCAL_COPY(word);
	where = my_atol((placeholder = function_rmatch(word)));
	new_free(&placeholder);

	if (where < 1)
		RETURN_EMPTY;

	placeholder = extractw(lame, 1, where - 1);
	return placeholder;
}
		
/* Same as above, but includes the word being matched */
BUILT_IN_FUNCTION(function_tow, word)
{
	int     where;
	char	*lame = (char *) 0;
	char	*placeholder;

	lame = LOCAL_COPY(word);
	where = my_atol((placeholder = function_rmatch(word)));
	new_free(&placeholder);

	if (where < 1)
		RETURN_EMPTY;

	placeholder = extractw(lame, 1, where);
	return placeholder;
}

/* Returns the string after the word being matched */
BUILT_IN_FUNCTION(function_afterw, word)
{
	int     where;
	char	*lame = (char *) 0;
	char	*placeholder;

	lame = m_strdup(word);
	placeholder = function_rmatch(word);
	where = my_atol(placeholder);

	new_free(&placeholder);

	if (where < 1)
	{
		new_free(&lame);
		RETURN_EMPTY;
	}
	placeholder = extractw(lame, where + 1, EOS);
	new_free(&lame);
	return placeholder;
}

/* Returns the string starting with the word being matched */
BUILT_IN_FUNCTION(function_fromw, word)
{
	int     where;
	char 	*lame = (char *) 0;
	char	*placeholder;

	lame = m_strdup(word);
	placeholder = function_rmatch(word);
	where = my_atol(placeholder);

	new_free(&placeholder);

	if (where < 1)
	{
		new_free(&lame);
		RETURN_EMPTY;
	}

	placeholder = extractw(lame, where, EOS);
	new_free(&lame);
	return placeholder;
}

/* Cut and paste a string */
BUILT_IN_FUNCTION(function_splice, word)
{
	char    *variable;
	int	start;
	int	length;
	char 	*left_part = NULL;
	char	*middle_part = NULL;
	char	*right_part = NULL;
	char	*old_value = NULL;
	char	*new_value = NULL;
	int	num_words;

	GET_STR_ARG(variable, word);
	GET_INT_ARG(start, word);
	GET_INT_ARG(length, word);

	old_value = get_variable(variable);
	num_words = word_count(old_value);

	if (start < 0)
	{
		if ((length += start) <= 0)
			RETURN_EMPTY;
		start = 0;
	}

	if (start >= num_words)
	{
		left_part = m_strdup(old_value);
		middle_part = m_strdup(empty_string);
		right_part = m_strdup(empty_string);
	}

	else if (start + length >= num_words)
	{
		left_part = extractw(old_value, 0, start - 1);
		middle_part = extractw(old_value, start, EOS);
		right_part = m_strdup(empty_string);
	}

	else
	{
		left_part = extractw(old_value, 0, start - 1);
		middle_part = extractw(old_value, start, start + length - 1);
		right_part = extractw(old_value, start + length, EOS);
	}

	new_value = NULL;
	malloc_strcpy(&new_value, left_part);
	if (new_value && *new_value && word && *word)
		malloc_strcat(&new_value, space);
	if (word && *word)
		malloc_strcat(&new_value, word);
	if (new_value && *new_value && *right_part && *right_part)
		malloc_strcat(&new_value, space);
	if (right_part && *right_part)
		malloc_strcat(&new_value, right_part);

	add_var_alias(variable, new_value, 0);

	new_free(&old_value);
	new_free(&new_value);
	new_free(&left_part);
	new_free(&right_part);
	return middle_part;
}

/* Worked over by jfn on 3/20/97.  If it breaks, yell at me. */
BUILT_IN_FUNCTION(function_numonchannel, word)
{
	RETURN_INT(number_on_channel(new_next_arg(word, &word), from_server));
}
	
/* Worked over by jfn on 3/20/97.  If it breaks, yet all me. */
/*
 * ircII2.8.2 defines $onchannel as:
 *   $onchannel(nick channel)
 * Which returns 1 if nick is on channel, 0, if not.
 * It returns empty on some kind of bizarre error.
 *
 * We had previously defined $onchannel as:
 *   $onchannel(channel)
 * Which returned everyone on the given channel
 * Which is a synonym for ircII2.8.2's $chanusers().
 */
BUILT_IN_FUNCTION(function_onchannel, word)
{
	char		*channel;
	char		*nicks = NULL;

	channel = new_next_arg(word, &word);
	if ((nicks = create_nick_list(channel, from_server)))
		return nicks;
	else
	{
		if (!nicks && (!word || !*word))
			RETURN_EMPTY;

		nicks = channel;
		channel = new_next_arg(word, &word);
		RETURN_INT(is_on_channel(channel, nicks) ? 1 : 0);
	}
}

BUILT_IN_FUNCTION(function_chops, word)
{
	char		*channel;

	channel = new_next_arg(word, &word);
	return create_chops_list(channel, from_server);
}

BUILT_IN_FUNCTION(function_nochops, word)
{
	char		*channel;

	channel = new_next_arg(word, &word);
	return create_nochops_list(channel, from_server);
}

/* Worked over by jfn on 3/20/97.  If it breaks, yet all me. */
BUILT_IN_FUNCTION(function_key, word)
{
	char		*channel;
	char    	*booya = (char *) 0;
	const char 	*key;
	size_t		rvclue=0;

	do
	{
		channel = new_next_arg(word, &word);
		if ((!channel || !*channel) && booya)
			break;

		key = get_channel_key(channel, from_server);
		m_sc3cat(&booya, space, (key && *key) ? key : "*", &rvclue);
	}
	while (word && *word);

	return (booya ? booya : m_strdup(empty_string));
}

/*
 * Based on a contribution made a long time ago by wintrhawk
 */
/* Worked over by jfn on 3/20/97.  If it breaks, yet all me. */
BUILT_IN_FUNCTION(function_channelmode, word)
{
	char	*channel;
	char    *booya = (char *) 0;
	char	*mode;
	size_t	rvclue=0;

	do
	{
		channel = new_next_arg(word, &word);
		if ((!channel || !*channel) && booya)
			break;

		mode = get_channel_mode(channel, from_server);
		m_sc3cat(&booya, space, (mode && *mode) ? mode : "*", &rvclue);
	}
	while (word && *word);

	return (booya ? booya : m_strdup(empty_string));
}



BUILT_IN_FUNCTION(function_revw, words)
{
	char *booya = NULL;
	size_t  rvclue=0, wclue=0;

	while (words && *words)
		m_sc3cat(&booya, space, last_arg(&words, &wclue), &rvclue);

	if (!booya)
		RETURN_EMPTY;

	return booya;
}

BUILT_IN_FUNCTION(function_reverse, words)
{
	int     length = strlen(words);
	char    *booya = (char *) 0;
	int     x = 0;

	booya = (char *)new_malloc(length+1);
	for(length--; length >= 0; length--,x++)
		booya[x] = words[length];
	booya[x]='\0';
	return (booya);
}

/*
 * To save time, try to precalc the size of the output buffer.
 * jfn 03/18/98
 */
BUILT_IN_FUNCTION(function_jot, input)
{
	int     start 	= 0;
	int     stop 	= 0;
	int     interval = 1;
	int     counter;
	char	*booya 	= NULL;
	size_t	clue = 0;

        GET_INT_ARG(start,input)
        GET_INT_ARG(stop, input)
        if (input && *input)
                GET_INT_ARG(interval, input)
        else
                interval = 1;

	if (interval == 0)
		RETURN_EMPTY;
        if (interval < 0) 
                interval = -interval;

        if (start < stop)
	{
		for (counter = start; 
		     counter <= stop; 
		     counter += interval)
		{
			m_sc3cat(&booya, space, ltoa(counter), &clue);
		}
	}
        else
	{
		for (counter = start; 
		     counter >= stop; 
		     counter -= interval)
		{
			m_sc3cat(&booya, space, ltoa(counter), &clue);
		}
	}

	return booya;
}

char *function_shift (char *word)
{
	char    *value = (char *) 0;
	char    *var    = (char *) 0;
	char	*booya 	= (char *) 0;
	char    *placeholder;

	GET_STR_ARG(var, word);

	if (word && *word)
		RETURN_STR(var);

	value = get_variable(var);

	if (!value && !*value)
		RETURN_EMPTY;

	placeholder = value;
	booya = m_strdup(new_next_arg(value, &value));
	if (var)
		add_var_alias(var, value, 0);
	new_free(&placeholder);
	if (!booya)
		RETURN_EMPTY;
	return booya;
}

char *function_unshift (char *word)
{
	char    *value = (char *) 0;
	char    *var    = (char *) 0;
	char	*booya  = (char *) 0;

	GET_STR_ARG(var, word);
	value = get_variable(var);
	if (!word || !*word)
		return value;

	booya = m_strdup(word);
	m_s3cat_s(&booya, space, value);

	add_var_alias(var, booya, 0);
	new_free(&value);
	return booya;
}

char *function_push (char *word)
{
	char    *value = (char *) 0;
	char    *var    = (char *) 0;

	GET_STR_ARG(var, word);
	upper(var);
	value = get_variable(var);
	m_s3cat(&value, space, word);
	add_var_alias(var, value, 0);
	return value;
}

char *function_pop (char *word)
{
	char *value	= (char *) 0;
	char *var	= (char *) 0;
	char *pointer	= (char *) 0;
	char *blech     = (char *) 0;

	GET_STR_ARG(var, word);

	if (word && *word)
	{
		pointer = strrchr(word, ' ');
		RETURN_STR(pointer ? pointer : word);
	}

	upper(var);
	value = get_variable(var);
	if (!value || !*value)
	{
		new_free(&value);
		RETURN_EMPTY;
	}

	if (!(pointer = strrchr(value, ' ')))
	{
		add_var_alias(var, empty_string, 0); /* dont forget this! */
		return value;	/* one word -- return it */
	}

	*pointer++ = 0;
	add_var_alias(var, value, 0);

	/* 
	 * because pointer points to value, we *must* make a copy of it
	 * *before* we free value! (And we cant forget to free value, either)
	 */
	blech = m_strdup(pointer);
	new_free(&value);
	return blech;
}


/* 
 * Search and replace function --
 * Usage:   $sar(c/search/replace/data)
 * Commands:
 *		r - treat data as a variable name and 
 *		    return the replaced data to the variable
 * 		g - Replace all instances, not just the first one
 *  The delimiter may be any character that is not a command (typically /)
 *  The delimiter MUST be the first character after the command
 *  Returns empty string on syntax error
 *  Returns 'data' if 'search' is of zero length.
 *
 * Note: Last rewritten on December 2, 1999
 */
BUILT_IN_FUNCTION(function_sar, word)
{
	char	delimiter;
	char *	replacing;
	size_t	replacinglen;
	char *	replace_with;
	char *	text;
	char *	keep_text;
	char *	pointer;
	char *	retval = NULL;
	char *	(*func) (const char *, const char *) = strstr;
	int	variable = 0, global = 0;
	size_t	rvclue=0;
	
	/*
	 * Scan the leading part of the argument list, slurping up any
	 * options, and grabbing the delimiter character.  If we don't
	 * come across a delimiter, then just end abruptly.
	 */
	for (;; word++)
	{
		if (*word == 'r')
			variable = 1;
		else if (*word == 'g')
			global = 1;
		else if (*word == 'i')
			func = stristr;
		else if (!*word)
			RETURN_EMPTY;
		else
		{
			delimiter = *word++;
			break;
		}
	}

	/*
	 * Now at this point, we have all the options slurped up and we
	 * also have 'word' pointing to the part to substitute.
	 * Look for the delimiter, which ends the "replacing" field.
	 */
	replacing = word;
	word = strchr(replacing, delimiter);
	if (word == NULL)
		RETURN_EMPTY;		/* Syntax error */
	*word++ = 0;
	replacinglen = strlen(replacing);

	/*
	 * Now at this point, we have all of the above, and also have
	 * the string to be replaced in 'replacing', and 'word' is 
	 * pointing at the string to replace it with.
	 * Look for the delimiter, which ends the 'replace_with' field.
	 */
	replace_with = word;
	word = strchr(replace_with, delimiter);
	if (word == NULL)
		RETURN_EMPTY;		/* Syntax error */
	*word++ = 0;

	/*
	 * Now at this point, we have all of the above, and we have the
	 * 'replace_with' field all set up, and so 'word' points at the
	 * operative text.  It could also be a variable name.  So we
	 * figure out what the operative text will be.
	 */
	if (variable)
		text = get_variable(word);
	else
		text = m_strdup(word);

	/* If there is no operative text, then return nothing. */
	if (!text)
		RETURN_EMPTY;
	if (!*text)
	{
		new_free(&text);
		RETURN_EMPTY;
	}

	/* If the replacing string is empty, then punt right here */
	if (replacinglen == 0)
		return text;

	keep_text = text;
	do
	{
		/* Look for the string in the text */
		pointer = func(keep_text, replacing);

		/* 
		 * If the pattern doesnt exist, or we're at the
		 * end of the string, then just stop right here.
		 */
		if (pointer == NULL || *pointer == 0)
			break;

		/*
		 * Now 'pointer' points at the first character in
		 * the string to replace.  We can NUL it out to 
		 * terminate the part before it that is interesting.
		 */
		pointer[0] = 0;

		/*
		 * We want to tack on the part of the string that
		 * is before the string to be replaced, and also
		 * the string that is to be doing the replacing.
		 */
		m_ec3cat(&retval, keep_text, replace_with, &rvclue);

		/*
		 * And now we want to step over the string and get
		 * 'keep_text' to point after that point.
		 */
		keep_text = pointer + replacinglen;

		/*
		 * If this is just a one-time through thing, then
		 * there won't be another repetition.
		 */
	}
	while (global);

	/*
	 * Tack on the last part of the string.
	 * This really does need to be here.
	 */
	malloc_strcat(&retval, keep_text);

	/*
	 * If we were operating on a variable, then put the value back
	 * into the variable now that we are done.  Remember that 'word'
	 * still points at the variable name.
	 */
	if (variable) 
		add_var_alias(word, retval, 0);

	/* 
	 * Free the 'text' we malloced before
	 */
	new_free(&text);

	/*
	 * And return the result.
	 */
	return retval;
}

BUILT_IN_FUNCTION(function_center, word)
{
	int	length,
		pad,
		width;
	char 	*padc;

	if (!word || !*word)
		RETURN_EMPTY;

	width = my_atol(new_next_arg(word, &word));
	length = strlen(word);
	
	if ((pad = width - length) < 0)
		RETURN_STR(word);

	pad /= 2;
	padc = (char *)new_malloc(width+1);
	memset(padc, ' ', pad);
	padc[pad] = 0;

	return strcat(padc, word);		/* XXX Should do strcpy here */
}

BUILT_IN_FUNCTION(function_split, word)
{
	char	*chrs;
	char	*pointer;

	chrs = next_arg(word, &word);
	pointer = word;
	while ((pointer = sindex(pointer,chrs)))
		*pointer++ = ' ';

	RETURN_STR(word);
}

BUILT_IN_FUNCTION(function_chr, word)
{
	char aboo[BIG_BUFFER_SIZE];
	char *ack = aboo;
	char *blah;

	while ((blah = next_arg(word, &word)))
		*ack++ = (char)my_atol(blah);

	*ack = '\0';
	RETURN_STR(aboo);
}

BUILT_IN_FUNCTION(function_ascii, word)
{
	char *aboo = NULL;
	size_t  rvclue=0;

	if (!word || !*word)
		RETURN_EMPTY;

	aboo = m_strdup(ltoa((long)(unsigned char)*word));
	while (*++word)
		m_c3cat(&aboo, space, ltoa((long)(unsigned char)*word), &rvclue);

	return aboo;
}

BUILT_IN_FUNCTION(function_which, word)
{
	char *file1;
	FILE *fp;

	GET_STR_ARG(file1, word);
	file1 = m_strdup(file1);

	if ((fp = uzfopen(&file1, (word && *word) ? word 
				: get_string_var(LOAD_PATH_VAR), 0)))
	{
		fclose(fp);
		return (file1);
	}
	else
	{
		new_free(&file1);
		RETURN_EMPTY;
	}
}


BUILT_IN_FUNCTION(function_isalpha, words)
{
	if (((*words >= 'a') && (*words <= 'z')) ||
	    ((*words >= 'A') && (*words <= 'Z')))
		RETURN_INT(1);
	else
		RETURN_INT(0);
}

BUILT_IN_FUNCTION(function_isdigit, words)
{
	if (((*words >= '0') && (*words <= '9')) ||
	    ((*words == '-') && ((*(words+1) >= '0') && (*(words+1) <= '9'))))
		RETURN_INT(1);
	else
		RETURN_INT(0);
}

BUILT_IN_FUNCTION(function_open, words)
{
	char *filename;
	GET_STR_ARG(filename, words);

	if (words && *words && toupper(*words) == 'R')
		RETURN_INT(open_file_for_read(filename));
	else if (words && *words && toupper(*words) == 'W')
		RETURN_INT(open_file_for_write(filename));
	else
		RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_close, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_close(my_atol(new_next_arg(words, &words))));
}	

BUILT_IN_FUNCTION(function_write, words)
{
	char *fdc;
	GET_STR_ARG(fdc, words);
	if (*fdc == 'w' || *fdc == 'W')
		RETURN_INT(file_write(1, my_atol(fdc + 1), words));
	else
		RETURN_INT(file_write(0, my_atol(fdc), words));
}

BUILT_IN_FUNCTION(function_writeb, words)
{
	char *fdc;
	GET_STR_ARG(fdc, words);
	if (*fdc == 'w' || *fdc == 'W')
		RETURN_INT(file_writeb(1, my_atol(fdc + 1), words));
	else
		RETURN_INT(file_writeb(0, my_atol(fdc), words));
}

BUILT_IN_FUNCTION(function_read, words)
{
	char *fdc = NULL, *numb = NULL;

	GET_STR_ARG(fdc, words);
	if (words && *words)
		GET_STR_ARG(numb, words);

	if (numb)
		return file_readb (my_atol(fdc), my_atol(numb));
	else
		return file_read (my_atol(fdc));
}

BUILT_IN_FUNCTION(function_seek, words)
{
	char *fdc = NULL, 
	     *numb = NULL, 
	     *whence = NULL;

	GET_STR_ARG(fdc, words);
	GET_STR_ARG(numb, words);
	GET_STR_ARG(whence, words);

	RETURN_INT(file_seek(my_atol(fdc), my_atol(numb), whence));
}

BUILT_IN_FUNCTION(function_eof, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_eof(my_atol(new_next_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_error, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_error(my_atol(new_next_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_skip, words)
{
	int arg1, arg2 = 1;
	GET_INT_ARG(arg1, words);
	if (words && *words)
		GET_INT_ARG(arg2, words);
	RETURN_INT(file_skip(arg1, arg2));
}

BUILT_IN_FUNCTION(function_isfilevalid, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_valid(my_atol(new_next_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_rewind, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_rewind(my_atol(new_next_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_iptoname, words)
{
	char	*ret = ip_to_host(words);
	RETURN_STR(ret);		/* Dont put function call in macro! */
}

BUILT_IN_FUNCTION(function_nametoip, words)
{
	char	*ret = host_to_ip(words);
	RETURN_STR(ret);		/* Dont put function call in macro! */
}

BUILT_IN_FUNCTION(function_convert, words)
{
	char	*ret = one_to_another(words);
	RETURN_STR(ret);		/* Dont put function call in macro! */
}

BUILT_IN_FUNCTION(function_translate, words)
{
	char *	oldc, 
	     *	newc, 
	     *	text,
	     *	ptr,
		delim;
	int 	size_old, 
		size_new,
		x;

	RETURN_IF_EMPTY(words);

	oldc = words;
	/* First character can be a slash.  If it is, we just skip over it */
	delim = *oldc++;
	newc = strchr(oldc, delim);

	if (!newc)
		RETURN_EMPTY;	/* no text in, no text out */

	text = strchr(++newc, delim);

	if (newc == oldc)
		RETURN_EMPTY;

	if (!text)
		RETURN_EMPTY;
	*text++ = '\0';

	if (newc == text)
	{
		*newc = '\0';
		newc = empty_string;
	}
	else
		newc[-1] = 0;

	/* this is cheating, but oh well, >;-) */
	text = m_strdup(text);

	size_new = strlen(newc);
	size_old = strlen(oldc);

	for (ptr = text; ptr && *ptr; ptr++)
	{
		for (x = 0; x < size_old; x++)
		{
			if (*ptr == oldc[x])
			{
				/* 
				 * Check to make sure we arent
				 * just eliminating the character.
				 * If we arent, put in the new char,
				 * otherwise strcpy it away
				 */
				if (size_new)
					*ptr = newc[(x<size_new)?x:size_new-1];
				else
				{
					ov_strcpy(ptr, ptr+1);
					ptr--;
				}
				break;
			}
		}
	}
	return text;
}

BUILT_IN_FUNCTION(function_server_version, word)
{
	int servnum;
	int version;

	servnum = ((word && *word) ? my_atol(next_arg(word, &word)) : primary_server);

	if (servnum > number_of_servers)
		RETURN_STR("unknown");

	version = get_server_version(servnum);

	if (version == Server2_8) 		RETURN_STR("2.8");
	else if (version == Server_u2_8) 	RETURN_STR("u2.8");
	else if (version == Server_u2_9) 	RETURN_STR("u2.9");
	else if (version == Server_u2_10)	RETURN_STR("u2.10");
	else if (version == Server2_9) 		RETURN_STR("2.9");
	else if (version == Server_u3_0)	RETURN_STR("u3.0");

	RETURN_STR("Unknown");
}

BUILT_IN_FUNCTION(function_unlink, words)
{
	char *	expanded;
	int 	failure = 0;

	while (words && *words)
	{
		if ((expanded = expand_twiddle(new_next_arg(words, &words))))
		{
			failure -= unlink(expanded);	
			new_free(&expanded);
		}
	}

	RETURN_INT(failure);
}

BUILT_IN_FUNCTION(function_rename, words)
{
	char *	filename1, 
	     *	filename2;
	char *expanded1, *expanded2;
	int 	failure = -1;

	GET_STR_ARG(filename1, words)
	if ((expanded1 = expand_twiddle(filename1)))
	{
		GET_STR_ARG(filename2, words)
		if ((expanded2 = expand_twiddle(filename2)))
		{
			failure = rename(expanded1, expanded2);
			new_free(&expanded2);
		}
		new_free(&expanded1);
	}

	RETURN_INT(failure);
}

BUILT_IN_FUNCTION(function_rmdir, words)
{
	int 	failure = 0;
	char *expanded;

	while (words && *words)
	{
		if ((expanded = expand_twiddle(new_next_arg(words, &words))))
		{
			failure -= rmdir(expanded);
			new_free(&expanded);
		}
		else
			failure++;
	}

	RETURN_INT(failure);
}

BUILT_IN_FUNCTION(function_truncate, words)
{
	int		num = 0;
	double		value = 0;
	char		buffer[BIG_BUFFER_SIZE],
			format[1024];

	GET_INT_ARG(num, words);
	GET_FLOAT_ARG(value, words);

	if (num < 0)
	{
		float foo;
		int end;

		sprintf(format, "%%.%de", -num-1);
		sprintf(buffer, format, value);
		foo = atof(buffer);
		sprintf(buffer, "%f", foo);
		end = strlen(buffer) - 1;
		if (end == 0)
			RETURN_EMPTY;
		while (buffer[end] == '0')
			end--;
		if (buffer[end] == '.')
			end--;
		buffer[end+1] = 0;
	}
	else if (num >= 0)
	{
		sprintf(format, "%%10.%dlf", num);
		sprintf(buffer, format, value);
	}
	else
		RETURN_EMPTY;

	while (*buffer == ' ')
		ov_strcpy(buffer, buffer+1);

	RETURN_STR(buffer);
}


/*
 * Apprantly, this was lifted from a CS client.  I reserve the right
 * to replace this code in future versions. (hop)
 */
/*
	I added this little function so that I can have stuff formatted
	into days, hours, minutes, seconds; but with d, h, m, s abreviations.
		-Taner
*/

BUILT_IN_FUNCTION(function_tdiff2, input)
{
	time_t	ltime;
	time_t	days,
		hours,
		minutes,
		seconds;
	char	tmp[80];
	char	*tstr;

	GET_INT_ARG(ltime, input);

	seconds = ltime % 60;
	ltime = (ltime - seconds) / 60;
	minutes = ltime%60;
	ltime = (ltime - minutes) / 60;
	hours = ltime % 24;
	days = (ltime - hours) / 24;
	tstr = tmp;

	if (days)
	{
		sprintf(tstr, "%ldd ", days);
		tstr += strlen(tstr);
	}
	if (hours)
	{
		sprintf(tstr, "%ldh ", hours);
		tstr += strlen(tstr);
	}
	if (minutes)
	{
		sprintf(tstr, "%ldm ", minutes);
		tstr += strlen(tstr);
	}
	if (seconds || (!days && !hours && !minutes))
	{
		sprintf(tstr, "%lds", seconds);
		tstr += strlen(tstr);
	}
	else
		*--tstr = 0;	/* chop off that space! */

	RETURN_STR(tmp);
}

/* 
 * The idea came from a CS client.  I wrote this from scratch, though.
 */
BUILT_IN_FUNCTION(function_utime, input)
{
	struct timeval tp;

	get_time(&tp);
	return m_sprintf("%ld %ld", (long)tp.tv_sec, (long)tp.tv_usec);
}


/*
 * This inverts any ansi sequence present in the string
 * from: Scott H Kilau <kilau@prairie.NoDak.edu>
 */
BUILT_IN_FUNCTION(function_stripansi, input)
{
	char	*cp;

	for (cp = input; *cp; cp++)
		/* Strip anything from 14 - 33, except ^O and ^V */
		if (*cp < 32 && *cp > 13)
			if (*cp != 15 && *cp != 22)
				*cp = (*cp & 127) | 64;

	RETURN_STR(input);
}

BUILT_IN_FUNCTION(function_servername, input)
{
	int 	sval = from_server;
	const char *	which;

	if (*input)
		GET_INT_ARG(sval, input);

	/* garbage in, garbage out. */
	if (sval < 0 || sval >= number_of_servers)
		RETURN_EMPTY;

	/* First we try to see what the server thinks it name is */
	which = get_server_itsname(sval);

	/* Next we try what we think its name is */
	if (!which)
		which = get_server_name(sval);

	/* Ok. i give up, return a null. */
	RETURN_STR(which);
}

BUILT_IN_FUNCTION(function_serverourname, input)
{
	int 	sval = from_server;
	const char *	which;

	if (*input)
		GET_INT_ARG(sval, input);

	/* garbage in, garbage out. */
	if (sval < 0 || sval >= number_of_servers)
		RETURN_EMPTY;

	/* Skip the itsname test. */

	/* Next we try what we think its name is */
	which = get_server_name(sval);

	/* Ok. i give up, return a null. */
	RETURN_STR(which);
}

BUILT_IN_FUNCTION(function_servergroup, input)
{
	int 	sval = from_server;
	const char *	which;

	if (*input)
		GET_INT_ARG(sval, input);

	/* garbage in, garbage out. */
	if (sval < 0 || sval >= number_of_servers)
		RETURN_EMPTY;

	/* Next we try what we think its name is */
	which = get_server_group(sval);

	/* Ok. i give up, return a null. */
	RETURN_STR(which);
}

BUILT_IN_FUNCTION(function_lastserver, input)
{
	RETURN_INT(last_server);
}

BUILT_IN_FUNCTION(function_winserv, input)
{
	int win = 0;
	char *tmp;
	Window *winp;

	if (input && *input)
	{
		if ((tmp = new_next_arg(input, &input)))
			win = my_atol(tmp);
	}
	if ((winp = get_window_by_refnum(win)))
		RETURN_INT(winp->server);

	RETURN_INT(-1);
}

BUILT_IN_FUNCTION(function_numwords, input)
{
	RETURN_INT(word_count(input));
}

BUILT_IN_FUNCTION(function_strlen, input)
{
	RETURN_INT(strlen(input));
}

BUILT_IN_FUNCTION(function_aliasctl, input)
{
	extern char *aliasctl (char *);
	return aliasctl(input);
}

/* 
 * Next two contributed by Scott H Kilau (sheik), who for some reason doesnt 
 * want to take credit for them. *shrug* >;-)
 *
 * Deciding not to be controversial, im keeping the original (contributed)
 * semantics of these two functions, which is to return 1 on success and
 * -1 on error.  If you dont like it, then tough. =)  I didnt write it, and
 * im not going to second guess any useful contributions.
 */
BUILT_IN_FUNCTION(function_fexist, words)
{
        char	FileBuf[BIG_BUFFER_SIZE+1];
	char	*filename, *fullname;

	if (!(filename = new_next_arg(words, &words)))
		RETURN_INT(-1);

	if (*filename == '/')
		strlcpy(FileBuf, filename, BIG_BUFFER_SIZE);
	else if (*filename == '~') 
	{
		if (!(fullname = expand_twiddle(filename)))
			RETURN_INT(-1);

		strmcpy(FileBuf, fullname, BIG_BUFFER_SIZE);
		new_free(&fullname);
	}
	else 
	{
		getcwd(FileBuf, BIG_BUFFER_SIZE);
		strmcat(FileBuf, "/", BIG_BUFFER_SIZE);
		strmcat(FileBuf, filename, BIG_BUFFER_SIZE);
	}

	if (access(FileBuf, R_OK) == -1)
		RETURN_INT(-1);

	RETURN_INT(1);
}

BUILT_IN_FUNCTION(function_fsize, words)
{
        char	FileBuf[BIG_BUFFER_SIZE+1];
	char	*filename, *fullname;
        struct  stat    stat_buf;

	if (!(filename = new_next_arg(words, &words)))
		RETURN_INT(-1);

	if (*filename == '/')
		strlcpy(FileBuf, filename, BIG_BUFFER_SIZE);
	else if (*filename == '~') 
	{
		if (!(fullname = expand_twiddle(filename)))
			RETURN_INT(-1);

		strmcpy(FileBuf, fullname, BIG_BUFFER_SIZE);
		new_free(&fullname);
	}
	else 
	{
		getcwd(FileBuf, sizeof(FileBuf));
		strmcat(FileBuf, "/", BIG_BUFFER_SIZE);
		strmcat(FileBuf, filename, BIG_BUFFER_SIZE);
	}

	if (stat(FileBuf, &stat_buf) == -1)
		RETURN_INT(-1);

	RETURN_INT((int)stat_buf.st_size);	/* Might not be an int */
}

/* 
 * Contributed by CrowMan
 * I changed two instances of "RETURN_INT(result)"
 * (where result was a null pointer) to RETURN_STR(empty_string)
 * because i dont think he meant to return a null pointer as an int value.
 */
/*
 * $crypt(password seed)
 * What it does: Returns a 13-char encrypted string when given a seed and
 *    password. Returns zero (0) if one or both args missing. Additional
 *    args ignored.
 * Caveats: Password truncated to 8 chars. Spaces allowed, but password
 *    must be inside "" quotes.
 * Credits: Thanks to Strongbow for showing me how crypt() works.
 * Written by: CrowMan
 */
char *crypt(); 
BUILT_IN_FUNCTION(function_crypt, words)
{
        char pass[9];
        char seed[3];
        char *blah, *bleh;
	char *ret;

	GET_STR_ARG(blah, words)
	GET_STR_ARG(bleh, words)
	strmcpy(pass, blah, 8);
	strmcpy(seed, bleh, 2);
	ret = crypt(pass, seed);
	RETURN_STR(ret);		/* Dont call function in macro! */
}

BUILT_IN_FUNCTION(function_info, words)
{
	char *which;
	extern char *info_c_sum;

	GET_STR_ARG(which, words);

	     if (!my_strnicmp(which, "C", 1))
		RETURN_STR(compile_info);
	else if (!my_strnicmp(which, "O", 1))
		RETURN_STR(compile_time_options);
	else if (!my_strnicmp(which, "S", 1))
		RETURN_STR(info_c_sum);
	else if (!my_strnicmp(which, "W", 1))
		RETURN_INT(1);
	else if (!my_strnicmp(which, "M", 1))
		RETURN_INT((x_debug & DEBUG_NEW_MATH) == DEBUG_NEW_MATH);
	else if (!my_strnicmp(which, "V", 1))
		RETURN_STR(useful_info);
	else if (!my_strnicmp(which, "R", 1))
		RETURN_STR(ridiculous_version_name);
	else if (!my_strnicmp(which, "I", 1))
		RETURN_INT(commit_id);
	else
		RETURN_EMPTY;
	/* more to be added as neccesary */
}

BUILT_IN_FUNCTION(function_geom, words)
{
	/* Erf. CO and LI are ints. (crowman) */
	return m_sprintf("%d %d", current_term->TI_cols, 
				  current_term->TI_lines);
}

BUILT_IN_FUNCTION(function_pass, words)
{
	char *lookfor;
	char *final, *ptr;

	GET_STR_ARG(lookfor, words);
	final = (char *)new_malloc(strlen(words) + 1);
	ptr = final;

	while (*words)
	{
		if (strchr(lookfor, *words))
			*ptr++ = *words;
		words++;
	}

	*ptr = 0;
	return final;
}

BUILT_IN_FUNCTION(function_repeat, words)
{
	int	num;
	char *	final = NULL;
	char *	p;

	GET_INT_ARG(num, words);
	if (num < 1)
		RETURN_EMPTY;

	final = (char *)new_malloc(strlen(words) * num + 1);
	*final = 0;

	p = final;
	for (; num > 0; num--)
		p = stpcpy(p, words);
	return final;
}

BUILT_IN_FUNCTION(function_epic, words)
{
	RETURN_INT(1);
}

BUILT_IN_FUNCTION(function_winsize, words)
{
	int refnum;
	Window *win;

	if (words && *words)
	{
		GET_INT_ARG(refnum, words);
		win = get_window_by_refnum(refnum);
	}
	else
		win = current_window;

	if (!win)
		RETURN_EMPTY;

	RETURN_INT(win->display_size);
}


BUILT_IN_FUNCTION(function_umode, words)
{
	int   servnum;
	const char *ret;

	if (words && *words)
		GET_INT_ARG(servnum, words)
	else
		servnum = from_server;

	ret = get_umode(servnum);
	RETURN_STR(ret);		/* Dont pass function call to macro! */
}

static int sort_it (const void *one, const void *two)
{
	return my_stricmp(*(const char **)one, *(const char **)two);
}

BUILT_IN_FUNCTION(function_sort, words)
{
	int 	wordc;
	char 	**wordl;
	char	*retval;

	if (!(wordc = splitw(words, &wordl)))
		RETURN_EMPTY;

	qsort((void *)wordl, wordc, sizeof(char *), sort_it);
	retval = unsplitw(&wordl, wordc);
	RETURN_MSTR(retval);
}

static int num_sort_it (const void *one, const void *two)
{
	char *oneptr = *(char **)one;
	char *twoptr = *(char **)two;
	long val1, val2;

	while (*oneptr && *twoptr)
	{
		while (*oneptr && *twoptr && !(my_isdigit(oneptr)) && !(my_isdigit(twoptr)))
		{
			if (*oneptr != *twoptr)
				return (*oneptr - *twoptr);
			oneptr++, twoptr++;
		}

		if (!*oneptr || !*twoptr)
			break;

		val1 = strtol(oneptr, (char **)&oneptr, 0);
		val2 = strtol(twoptr, (char **)&twoptr, 0);
		if (val1 != val2)
			return val1 - val2;
	}
	return (*oneptr - *twoptr);
}

BUILT_IN_FUNCTION(function_numsort, words)
{
	int wordc;
	char **wordl;
	char *retval;

	if (!(wordc = splitw(words, &wordl)))
		RETURN_EMPTY;
	qsort((void *)wordl, wordc, sizeof(char *), num_sort_it);
	retval = unsplitw(&wordl, wordc);
	RETURN_MSTR(retval);
}


BUILT_IN_FUNCTION(function_notify, words)
{
	int showon = -1, showserver = from_server;
	char *firstw;

	while (words && *words)
	{
		firstw = new_next_arg(words, &words);
		if (!my_strnicmp(firstw, "on", 2))
		{
			showon = 1;
			continue;
		}
		if (!my_strnicmp(firstw, "off", 3))
		{
			showon = 0;
			continue;
		}
		if (!my_strnicmp(firstw, "serv", 4))
		{
			GET_INT_ARG(showserver, words);
		}
	}

	/* dont use RETURN_STR() here. */
	return get_notify_nicks(showserver, showon);
}

#ifdef NEED_GLOB
#define glob bsd_glob
#define globfree bsd_globfree
#endif

BUILT_IN_FUNCTION(function_glob, word)
{
	char 	*path, 
		*path2, 
		*retval = NULL;
	int 	numglobs, i;
	size_t	rvclue=0;
	glob_t 	globbers;

	memset(&globbers, 0, sizeof(glob_t));
	while (word && *word)
	{
		GET_STR_ARG(path, word);
		if (!(path2 = expand_twiddle(path)))
			path2 = m_strdup(path);

		if ((numglobs = glob(path2, GLOB_MARK, NULL, &globbers)) < 0)
		{
			new_free(&path2);
			RETURN_INT(numglobs);
		}

		for (i = 0; i < globbers.gl_pathc; i++)
		{
			if (strchr(globbers.gl_pathv[i], ' '))
			{
				char *b = alloca(strlen(globbers.gl_pathv[i]) + 4);
				sprintf(b, "\"%s\"", globbers.gl_pathv[i]);
				m_sc3cat(&retval, space, b, &rvclue);
			}
			else
				m_sc3cat(&retval, space, globbers.gl_pathv[i], &rvclue);
		}
		globfree(&globbers);
		new_free(&path2);
	}

	RETURN_IF_EMPTY(retval);
	return retval;
}

BUILT_IN_FUNCTION(function_globi, word)
{
	char 	*path, 
		*path2, 
		*retval = NULL;
	int 	numglobs, i;
	size_t	rvclue=0;
	glob_t 	globbers;

	memset(&globbers, 0, sizeof(glob_t));
	while (word && *word)
	{
		GET_STR_ARG(path, word);
		if (!(path2 = expand_twiddle(path)))
			path2 = m_strdup(path);

		if ((numglobs = bsd_glob(path2, GLOB_MARK | GLOB_INSENSITIVE, 
					NULL, &globbers)) < 0)
		{
			new_free(&path2);
			RETURN_INT(numglobs);
		}

		for (i = 0; i < globbers.gl_pathc; i++)
		{
			if (strchr(globbers.gl_pathv[i], ' '))
			{
				char *b = alloca(strlen(globbers.gl_pathv[i]) + 4);
				sprintf(b, "\"%s\"", globbers.gl_pathv[i]);
				m_sc3cat(&retval, space, b, &rvclue);
			}
			else
				m_sc3cat(&retval, space, globbers.gl_pathv[i], &rvclue);
		}
		bsd_globfree(&globbers);
		new_free(&path2);
	}

	RETURN_IF_EMPTY(retval);
	return retval;
}


BUILT_IN_FUNCTION(function_mkdir, words)
{
	int 	failure = 0;
	char *expanded;

	while (words && *words)
	{
		if ((expanded = expand_twiddle(new_next_arg(words, &words))))
		{
			failure -= mkdir(expanded, 0777);
			new_free(&expanded);
		}
		else
			failure++;
	}

	RETURN_INT(failure);
}

BUILT_IN_FUNCTION(function_umask, words)
{
	int new_umask;
	GET_INT_ARG(new_umask, words);
	RETURN_INT(umask(new_umask));
}

BUILT_IN_FUNCTION(function_chmod, words)
{
	char 	*filearg, 
		*after;
	int 	fd = -1;
	char 	*perm_s;
	mode_t 	perm;
	char 	*expanded;
	int	retval;

	GET_STR_ARG(filearg, words);
	fd = (int) strtoul(filearg, &after, 0);

	GET_STR_ARG(perm_s, words);
	perm = (mode_t) strtol(perm_s, &perm_s, 8);

	if (after != words && *after == 0)
	{
		if (file_valid(fd))
			RETURN_INT(fchmod(fd, perm));
		else
			RETURN_EMPTY;
	}
	else
	{
		if ((expanded = expand_twiddle(filearg)))
		{
			retval = chmod(expanded, perm);
			new_free(&expanded);
			RETURN_INT(retval);
		}
		RETURN_INT(-1);
	}
}

BUILT_IN_FUNCTION(function_twiddle, words)
{
	char *	retval = NULL;

	if (words && *words)
		retval = expand_twiddle(new_next_arg(words, &words));

	RETURN_MSTR(retval);
}


static int unsort_it (const void *one, const void *two)
{
	/* This just makes me itch. ;-) */
	return (int)(*(const char **)one - *(const char **)two);
}
/* 
 * Date: Sun, 29 Sep 1996 19:17:25 -0700
 * Author: Thomas Morgan <tmorgan@pobox.com>
 * Submitted-by: archon <nuance@twri.tamu.edu>
 *
 * $uniq (string of text)
 * Given a set of words, returns a new set of words with duplicates
 * removed.
 * EX: $uniq(one two one two) returns "one two"
 */
BUILT_IN_FUNCTION(function_uniq, word)
{
        char    **list = NULL, *booya = NULL;
        int     listc, listi, listo;

	RETURN_IF_EMPTY(word);
        listc = splitw(word, &list);

	/* 'list' is set to NULL if 'word' is empty.  Punt in this case. */
	if (!list)
		RETURN_EMPTY;

#if 1
	/*
	 * This was originally conceved by wd, although his code
	 * never made it into the distribution.
	 *
	 * Sort followed up with a remove duplicates.  Standard stuff,
	 * only, the whacky way we go about it is due to compatibility.
	 *
	 * The major assumption here is that the pointers in list[] are
	 * in ascending order.  Since splitw() works by inserting nuls
	 * into the original string, we can be somewhat secure here.
	 */
	qsort((void *)list, listc, sizeof(char *), sort_it);

	/* Remove _subsequent_ duplicate values wrt the original list
	 * This means, kill the higher valued pointers value.
	 */
	for (listo = 0, listi = 1; listi < listc; listi++) {
		if (sort_it(&list[listi],&list[listo])) {
			listo = listi;
		} else {
			if (list[listi]<list[listo]) {
				list[listo][0] = 0;
				listo = listi;
			} else {
				list[listi][0] = 0;
			}
		}
	}

	/* We want the remaining words to appear in their original order */
	qsort((void *)list, listc, sizeof(char *), unsort_it);
	booya = unsplitw(&list, listc);

#else
    { /* Oh hush.  It is #ifdef'd out, after all. */
	char	*tval;
	char	*input;
	size_t	rvclue=0;

	/*
	 * XXX This algorithm is too expensive.  It should be done
	 * by some other way.  Calling function_findw() is a hack.
	 */

	/* The first word is always obviously included. */
	booya = m_strdup(list[0]);
        for (listi = 1; listi < listc; listi++)
        {
		input = alloca(strlen(list[listi]) + strlen(booya) + 2);
		stpcpy(stpcpy(stpcpy(input, list[listi]), space), booya);

		tval = function_findw(input);
		if (my_atol(tval) == -1)
			m_sc3cat(&booya, space, list[listi], &rvclue);
		new_free(&tval);
        }
    }
#endif

        new_free((char **)&list);
	RETURN_MSTR(booya);
}

BUILT_IN_FUNCTION(function_winvisible, word)
{
	RETURN_INT(is_window_visible(word));
}

BUILT_IN_FUNCTION(function_status, word)
{
	unsigned 	window_refnum;
	int 		status_line;
	char *		retval;

	GET_INT_ARG(window_refnum, word);
	GET_INT_ARG(status_line, word);
	retval = get_status_by_refnum(window_refnum, status_line);
	RETURN_STR(retval);
}

/*
 * Date: Sat, 26 Apr 1997 13:41:11 +1200
 * Author: IceKarma (ankh@canuck.gen.nz)
 * Contributed by: author
 *
 * Usage: $winchan(#channel <server refnum|server name>)
 * Given a channel name and either a server refnum or a direct server
 * name or an effective server name, this function will return the 
 * refnum of the window where the channel is the current channel (on that
 * server if appropriate.)
 *
 * Returns -1 (Too few arguments specified, or Window Not Found) on error.
 */
BUILT_IN_FUNCTION(function_winchan, input)
{
	char *arg1 = NULL;

	if (input && *input)
		GET_STR_ARG(arg1, input);

	/*
	 * Return window refnum by channel
	 */
	if (arg1 && is_channel(arg1))
	{
		int 	servnum = from_server;
		char 	*chan, 
			*serv = NULL;
		Window 	*winp = NULL;	/* XXXX */

		chan = arg1;
		if ((serv = new_next_arg(input, &input)))
		{
			if (my_isdigit(serv))
				servnum = my_atol(serv);
			else
				servnum = find_in_server_list(serv, 0);
		}

		/* Now return window for *any* channel. */
		if ((winp = get_channel_window(chan, servnum)))
			RETURN_INT(winp->refnum);

		RETURN_INT(-1);
	}

	/*
	 * Return channel by window refnum/desc
	 */
	else 
	{
		Window *win = current_window;

		if (arg1 && *arg1)
			win = get_window_by_desc(arg1);

		if (!win)
			RETURN_EMPTY;

		RETURN_STR(win->current_channel);
	}
}

BUILT_IN_FUNCTION(function_findw, input)
{
	char	*word, *this_word;
	int	word_cnt = 0;

	GET_STR_ARG(word, input);

	while (input && *input)
	{
		GET_STR_ARG(this_word, input);
		if (!my_stricmp(this_word, word))
			RETURN_INT(word_cnt);

		word_cnt++;
	}

	RETURN_INT(-1);
}


BUILT_IN_FUNCTION(function_uhc, input)
{
	char 	*nick, *user, *host;

	if (!input || !*input)
		RETURN_EMPTY;

	if (figure_out_address(input, &nick, &user, &host))
		RETURN_STR(input);
	else
		return m_sprintf("%s!%s@%s", nick, user, host);
}

/*
 * Opposite of $uhc().  Breaks down a complete nick!user@host
 * into smaller components, based upon absolute wildcards.
 * Eg, *!user@host becomes user@host.  And *!*@*.host becomes *.host
 * Based on the contribution and further changes by Peter V. Evans 
 * (arc@anet-chi.com)
 */
BUILT_IN_FUNCTION(function_deuhc, input)
{
	char	*buf;
 
	buf = LOCAL_COPY(input);
 
	if (!strchr(buf, '!') || !strchr(buf, '@'))
		RETURN_EMPTY;

	if (!strncmp(buf, "*!", 2))
	{
		buf += 2;
		if (!strncmp(buf, "*@", 2))
			buf += 2;
	}

	RETURN_STR(buf);
}


/* 
 * Given a channel, tells you what window its bound to.
 * Given a window, tells you what channel its bound to.
 */
BUILT_IN_FUNCTION(function_winbound, input)
{
	Window *foo;
	char *	stuff;
	char *	retval;

	if (!(stuff = new_next_arg(input, &input)))
		RETURN_EMPTY;
	if (my_atol(stuff) && (foo = get_window_by_refnum((unsigned)my_atol(stuff))))
	{
		retval = get_bound_channel(foo);
		RETURN_STR(retval);
	}
	else if ((foo = get_window_by_name(stuff)))
	{
		retval = get_bound_channel(foo);
		RETURN_STR(retval);
	}
	else if ((foo = get_window_bound_channel(stuff)))
	{
		retval = get_refnum_by_window(foo);
		RETURN_STR(retval);
	}

	RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_ftime, words)
{
        char	FileBuf[BIG_BUFFER_SIZE+1];
	char	*filename, *fullname;
	struct stat s;

	if ((filename = new_next_arg(words, &words)))
	{
		if (*filename == '/')
			strlcpy(FileBuf, filename, BIG_BUFFER_SIZE);

		else if (*filename == '~') 
		{
			if (!(fullname = expand_twiddle(filename)))
				RETURN_EMPTY;

			strmcpy(FileBuf, fullname, BIG_BUFFER_SIZE);
			new_free(&fullname);
		}
		else 
		{
			getcwd(FileBuf, BIG_BUFFER_SIZE);
			strmcat(FileBuf, "/", BIG_BUFFER_SIZE);
			strmcat(FileBuf, filename, BIG_BUFFER_SIZE);
		}

		if (stat(FileBuf, &s) == -1)
			RETURN_EMPTY;
		else
			RETURN_INT(s.st_mtime);
	}
	RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_irclib, input)
{
	RETURN_STR(irc_lib);
}

BUILT_IN_FUNCTION(function_substr, input)
{
	char *search;
	char *ptr;

	GET_STR_ARG(search, input);

	if ((ptr = stristr(input, search)))
		RETURN_INT((unsigned long)(ptr - input));
	else
		RETURN_INT(-1);
}

BUILT_IN_FUNCTION(function_rsubstr, input)
{
	char *search;
	char *ptr;

	GET_STR_ARG(search, input);

	if ((ptr = rstristr(input, search)))
		RETURN_INT((unsigned long)(ptr - input));
	else
		RETURN_INT(-1);
}

BUILT_IN_FUNCTION(function_nohighlight, input)
{
	char *outbuf, *ptr;

	ptr = outbuf = alloca(strlen(input) * 3 + 1);
	while (*input)
	{
		switch (*input)
		{
			case REV_TOG:
			case UND_TOG:
			case BOLD_TOG:
			case BLINK_TOG:
			case ALL_OFF:
			case ALT_TOG:
			case '\003':
			case '\033':
			{
				*ptr++ = REV_TOG;
				*ptr++ = (*input++ | 0x40);
				*ptr++ = REV_TOG;
				break;
			}
			default:
			{
				*ptr++ = *input++;
				break;
			}
		}
	}

	*ptr = 0;
	RETURN_STR(outbuf);
}

BUILT_IN_FUNCTION(function_servernick, input)
{
	char *	servdesc;
	int 	refnum;
	const char *	retval;

	if (*input)
	{
		GET_STR_ARG(servdesc, input);
		if ((refnum = parse_server_index(servdesc)) == -1)
			if ((refnum = find_in_server_list(servdesc, 0)) == -1)
				RETURN_EMPTY;
	}
	else if (from_server != -1)
		refnum = from_server;
	else
		RETURN_EMPTY;

	retval = get_server_nickname(refnum);
	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_winnames, input)
{
	Window *win;

	if (*input)
		win = get_window_by_desc(input);
	else
		win = current_window;

	if (!win)
		RETURN_EMPTY;

	return get_nicklist_by_window(win);
}

BUILT_IN_FUNCTION(function_isconnected, input)
{
	int refnum;

	/*
	 * Allow no arguments to reference from_server
	 */
	if (!*input)
		refnum = from_server;
	else
		GET_INT_ARG(refnum, input);

	if (refnum == -1)
		refnum = from_server;

	RETURN_INT(is_server_connected(refnum));
}

BUILT_IN_FUNCTION(function_currchans, input)
{
	int server = -1;
	Window *blah = NULL;
	char *retval = NULL;

	if (input && *input)
		GET_INT_ARG(server, input)
	else
		server = -2;

	if (server == -1)
		server = from_server;

	while (traverse_all_windows(&blah))
	{
		if (server != -2 && blah->server != server)
			continue;
		if (!blah->current_channel)
			continue;

		m_s3cat(&retval, space, "\"");
		malloc_strcat(&retval, ltoa(blah->server));
		malloc_strcat(&retval, space);
		malloc_strcat(&retval, blah->current_channel);
		malloc_strcat(&retval, "\"");
	}

	RETURN_MSTR(retval);
}

/*
 * Returns 1 if the specified function name is a built in function,
 * Returns 0 if not.
 */
BUILT_IN_FUNCTION(function_fnexist, input)
{
	char *fname;

	GET_STR_ARG(fname, input);
	RETURN_INT(func_exist(fname));
}

BUILT_IN_FUNCTION(function_cexist, input)
{
	char *fname;

	GET_STR_ARG(fname, input);
	RETURN_INT(command_exist(fname));
}


/*
 * These are used as an interface to regex support.  Here's the plan:
 *
 * $regcomp(<pattern>) 
 *	will return an $encode()d string that is suitable for
 * 		assigning to a variable.  The return value of this
 *		function must at some point be passed to $regfree()!
 *
 * $regexec(<compiled> <string>)
 *	Will return "0" or "1" depending on whether or not the given string
 *		was matched by the previously compiled string.  The value
 *		for <compiled> must be a value previously returned by $regex().
 *		Failure to do this will result in undefined chaos.
 *
 * $regerror(<compiled>)
 *	Will return the error string for why the previous $regexec() or 
 *		$regex() call failed.
 *
 * $regfree(<compiled>)
 *	Will free off any of the data that might be contained in <compiled>
 *		You MUST call $regfree() on any value that was previously
 *		returned by $regex(), or you will leak memory.  This is not
 *		my problem (tm).  It returns the FALSE value.
 */

#ifdef HAVE_REGEX_H
static int last_regex_error = 0; 		/* XXX */

BUILT_IN_FUNCTION(function_regcomp, input)
{
	regex_t preg;

	last_regex_error = regcomp(&preg, input, REG_EXTENDED | REG_ICASE);
	return encode((char *)&preg, sizeof(regex_t));
}

BUILT_IN_FUNCTION(function_regexec, input)
{
	char *unsaved;
	regex_t *preg;

	GET_STR_ARG(unsaved, input);
	preg = (regex_t *)decode(unsaved);
	last_regex_error = regexec(preg, input, 0, NULL, 0);
	new_free((char **)&preg);
	RETURN_INT(last_regex_error);	/* DONT PASS FUNC CALL TO RETURN_INT */
}

BUILT_IN_FUNCTION(function_regmatches, input)
{
	char *unsaved, *ret = NULL;
	size_t nmatch, n, foo, clue = 0;
	regex_t *preg;
	regmatch_t *pmatch = NULL;

	GET_STR_ARG(unsaved, input);
	GET_INT_ARG(nmatch, input);
	preg = (regex_t *)decode(unsaved);
	RESIZE(pmatch, regmatch_t, nmatch);
	if (!(last_regex_error = regexec(preg, input, nmatch, pmatch, 0)))
		for (n = 0; n < nmatch; n++) {
			if (0 > pmatch[n].rm_so)
				/* This may need to be continue depending
				 * on the regex implementation */
				break;
			m_sc3cat(&ret, space, ltoa(foo = pmatch[n].rm_so), &clue);
			m_sc3cat(&ret, space, ltoa(pmatch[n].rm_eo - foo), &clue);
		}
	new_free((char **)&preg);
	new_free((char **)&pmatch);
	RETURN_MSTR(ret);	/* DONT PASS FUNC CALL TO RETURN_INT */
}

BUILT_IN_FUNCTION(function_regerror, input)
{
	char *unsaved;
	regex_t *preg;
	char error_buf[1024] = "";

	GET_STR_ARG(unsaved, input);
	preg = (regex_t *)decode(unsaved);

	if (last_regex_error)
		regerror(last_regex_error, preg, error_buf, 1024);
	new_free((char **)&preg);
	RETURN_STR(error_buf);
}

BUILT_IN_FUNCTION(function_regfree, input)
{
	char *unsaved;
	regex_t *preg;

	GET_STR_ARG(unsaved, input);
	preg = (regex_t *)decode(unsaved);
	regfree(preg);
	new_free((char **)&preg);
	RETURN_EMPTY;
}

#else
BUILT_IN_FUNCTION(function_regexec, input)  { RETURN_EMPTY; }
BUILT_IN_FUNCTION(function_regcomp, input)  { RETURN_EMPTY; }
BUILT_IN_FUNCTION(function_regerror, input) { RETURN_STR("no regex support"); }
BUILT_IN_FUNCTION(function_regfree, input)  { RETURN_EMPTY; }
#endif

BUILT_IN_FUNCTION(function_getenv, input)
{
	char *env;

	GET_STR_ARG(env, input);
	RETURN_STR(getenv(env));
}

BUILT_IN_FUNCTION(function_count, input)
{
	char 	*str, *str2;
	int 	count = 0;

	GET_STR_ARG(str, input);
	str2 = input;

	for (;;)
	{
		if ((str2 = stristr(str2, str)))
			count++, str2++;
		else
			break;
	}

	RETURN_INT(count);
}

/*
 * Usage: $igmask(pattern)
 * Returns: All the ignore patterns that are matched by the pattern
 * Example: $igmask(*) returns your entire ignore list
 * Based on work contributed by pavlov
 */
BUILT_IN_FUNCTION(function_igmask, input)
{
	char *	retval;
	retval = get_ignores_by_pattern(input, 0);
	RETURN_MSTR(retval);
}

/*
 * Usage: $rigmask(pattern)
 * Returns: All the ignore patterns that would trigger on the given input
 * Example: $igmask(wc!bhauber@frenzy.com) would return a pattern like 
 *		*!*@*frenzy.com  or like *!bhauber@*
 */
BUILT_IN_FUNCTION(function_rigmask, input)
{
	char *	retval;
	retval = get_ignores_by_pattern(input, 1);
	RETURN_MSTR(retval);
}


/*
 * Usage: $randread(filename)
 * Returns: A psuedo-random line in the specified file
 * Based on work contributed by srfrog
 */
BUILT_IN_FUNCTION(function_randread, input)
{
	FILE 	*fp;
	char 	buffer[BIG_BUFFER_SIZE + 1];
	char 	*filename, *fullname = NULL;
	off_t	filesize, offset;

	*buffer = 0;
	GET_STR_ARG(filename, input);

	/*
	 * It was this or use a goto.  :P
	 */
	if (  ((fullname = expand_twiddle(filename)) == NULL) ||
	      ((filesize = file_size(fullname)) <= 0) ||
	      ((fp = fopen(fullname, "r")) == NULL) )
	{
		new_free(&fullname);
		RETURN_EMPTY;
	}

	offset = random_number(0) % filesize - 1;
	fseek(fp, offset, SEEK_SET);
	fgets(buffer, BIG_BUFFER_SIZE, fp);
	fgets(buffer, BIG_BUFFER_SIZE, fp);
	if (feof(fp))
	{
		fseek(fp, 0, SEEK_SET);
		fgets(buffer, BIG_BUFFER_SIZE, fp);
	}
	chomp(buffer);
	fclose(fp);

	new_free(&fullname);
	RETURN_STR(buffer);
}

/* 
 * Search and replace function --
 * Usage:   $msar(c/search/replace/data)
 *  Commands:
 *              r - treat data as a variable name and
 *                  return the replaced data to the variable
 *              g - Replace all instances, not just the first one
 *              i - does a stristr()
 * The delimiter may be any character that is not a command (typically /)
 * The delimiter MUST be the first character after the command
 * Returns empty string on error
 *
 * Written by panasync, Contributed on 12/03/1997
 */
BUILT_IN_FUNCTION(function_msar, word)
{
	char    delimiter;
	char    *pointer        = NULL;
	char    *search         = NULL;
	char    *replace        = NULL;
	char    *data           = NULL;
	char    *value          = NULL;
	char    *booya          = NULL;
	char    *p              = NULL;
	int     variable 	= 0,
		global 		= 0,
		searchlen;
	char 	*(*func) (const char *, const char *) = strstr;
	char    *svalue;

        while (((*word == 'r') && (variable = 1)) || ((*word == 'g') && (global
= 1)) || ((*word == 'i') && (func = stristr)))
                word++;

        RETURN_IF_EMPTY(word);

        delimiter = *word;
        search = word + 1;
        if (!(replace = strchr(search, delimiter)))
                RETURN_EMPTY;

        *replace++ = 0;
        if (!(data = strchr(replace,delimiter)))
                RETURN_EMPTY;

        *data++ = 0;

        if (!(p = strrchr(data, delimiter)))
                value = (variable == 1) ? get_variable(data) : m_strdup(data);
        else
        {
                *p++ = 0;
                value = (variable == 1) ? get_variable(p) : m_strdup(p);
        }

        if (!value || !*value)
        {
                new_free(&value);
                RETURN_EMPTY;
        }

        pointer = svalue = value;

        do 
        {
                searchlen = strlen(search) - 1;
                if (searchlen < 0)
                        searchlen = 0;
                if (global)
                {
                        while ((pointer = func(pointer,search)))
                        {
                                pointer[0] = pointer[searchlen] = 0;
                                pointer += searchlen + 1;
                                m_e3cat(&booya, value, replace);
                                value = pointer;
                                if (!*pointer)
                                        break;
                        }
                } 
                else
                {
                        if ((pointer = func(pointer,search)))
                        {
                                pointer[0] = pointer[searchlen] = 0;
                                pointer += searchlen + 1;
                                m_e3cat(&booya, value, replace);
                                value = pointer;
                        }
                }
                malloc_strcat(&booya, value);
                if (data && *data)
                {
                        search = data;
                        if ((replace = strchr(data, delimiter)))
                        {
                                *replace++ = 0;
                                if ((data = strchr(replace, delimiter)))
                                        *data++ = 0;
                        }
                        new_free(&svalue);
#if 0
			/* This is the old stuff */
                        pointer = svalue = value = booya;
                        booya = NULL;
                        if (!replace || !search)
                                break;
#else
			/* This was recommended by RoboHak */
			/* And he fixed it again, from Colten */
			if (!replace || !search)
			{
				pointer = value = svalue;
				break;
			}
			pointer = value = svalue = booya;
			booya = NULL;
#endif
                } else 
                        break;
        } while (1);

        if (variable) 
                add_var_alias(data, booya, 0);
        new_free(&svalue);
        return (booya);
}

/*
 * Usage: $leftpc(COUNT TEXT)
 * Return the *longest* initial part of TEXT that includes COUNT + 1 printable
 * characters. (Don't ask)
 */
BUILT_IN_FUNCTION(function_leftpc, word)
{
	u_char ** 	prepared = NULL;
	char *		retval;
	int		lines = 1;
	int		count;
	int		x;

	GET_INT_ARG(count, word);
	if (count < 0 || !*word)
		RETURN_EMPTY;

	x = normalize_permit_all_attributes;
	normalize_permit_all_attributes = 1;

	/* Convert the string to "attribute format" */
	word = normalize_string(word, 0);

	/* Count off the first line of stuff */
	prepared = prepare_display(word, count, &lines, PREPARE_NOWRAP);

	/* Convert the first line back to "logical format" */
	retval = denormalize_string(prepared[0]);

	normalize_permit_all_attributes = x;

	/* Clean up and return. */
	new_free(&word);
	return retval;
}


/*
 * This was written to have the same net-effect as bitchx.  However, we use
 * ^C codes rather than ansi codes as bitchx does.  You shouldnt notice one
 * way or the other.
 */
BUILT_IN_FUNCTION(function_cparse, input)
{
	int 	i, j, max;
	char 	*output;
	int	noappend = 0, expand = 0;
	char *	stuff;

	if (*input == '"')
	{
		stuff = new_next_arg(input, &input);
		expand = 1;
	}
	else
		stuff = input;

	output = (char *)alloca(strlen(stuff) * 3 + 4);

	for (i = 0, j = 0, max = strlen(stuff); i < max; i++)
	{
		if (stuff[i] == '%')
		{
		    i++;
		    switch (stuff[i])
		    {
			case 'k':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++] = '0';
				break;
			case 'r':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++] = '1';
				break;
			case 'g':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++] = '2';
				break;
			case 'y':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++] = '3';
				break;
			case 'b':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++]   = '4';
				break;
			case 'm':
			case 'p':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++]   = '5';
				break;
			case 'c':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++]   = '6';
				break;
			case 'w':
				output[j++] = '\003';
				output[j++] = '3';
				output[j++]   = '7';
				break;

			case 'K':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '0';
				break;
			case 'R':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '1';
				break;
			case 'G':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '2';
				break;
			case 'Y':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '3';
				break;
			case 'B':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '4';
				break;
			case 'M':
			case 'P':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '5';
				break;
			case 'C':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '6';
				break;
			case 'W':
				output[j++] = '\003';
				output[j++] = '5';
				output[j++]   = '7';
				break;

			case '0': case '1': case '2': case '3': 
			case '4': case '5': case '6': case '7':
				output[j++] = '\003';
				output[j++] = ',';
				output[j++] = '4';
				output[j++]   = stuff[i];
				break;

			case 'F':
				output[j++] = BLINK_TOG;
				break;
			case 'n':
				output[j++] = '\003';
				output[j++] = ALL_OFF;
				break;
			case 'N':
				noappend = 1;
				break;
			case '%':
				output[j++] = stuff[i];
				break;
			default:
				output[j++] = '%';
				output[j++] = stuff[i];
				break;
		    }
		}
		else
			output[j++] = stuff[i];
	}
	if (noappend == 0)
	{
		output[j++] = '\003';
		output[j++] = '-';
		output[j++] = '1';
	}
	output[j] = 0;

	if (expand)
	{
		int	af;
		stuff = expand_alias(output, input, &af, NULL);
		RETURN_MSTR(stuff);
	}

	RETURN_STR(output);
}

/*
 * $uname()
 * Returns system information.  Expandoes %a, %s, %r, %v, %m, and %n
 * correspond to uname(1)'s -asrvmn switches.  %% expands to a literal
 * '%'.  If no arguments are given, the function returns %s and %r (the
 * same information given in the client's ctcp version reply).
 *
 * Contributed by Matt Carothers (CrackBaby) on Dec 20, 1997
 */
BUILT_IN_FUNCTION(function_uname, input)
{
#ifndef HAVE_UNAME
	RETURN_STR("unknown");
#else
	struct utsname un;
	size_t	size = BIG_BUFFER_SIZE;
	char 	tmp[BIG_BUFFER_SIZE + 1];
	char *	ptr = tmp;
	int 	i;
	int	len;

	if (uname(&un) == -1)
		RETURN_STR("unknown");

	if (!*input)
		input = "%s %r";

	for (i = 0, len = strlen(input); i < len; i++)
	{
		if (ptr - tmp >= size)
			break;

		if (input[i] == '%') 
		{
		    switch (input[++i]) 
		    {
			case 'm':	strlcpy(ptr, un.machine, size);
					break;
			case 'n':	strlcpy(ptr, un.nodename, size);
					break;
			case 'r':	strlcpy(ptr, un.release, size);
					break;
			case 's':	strlcpy(ptr, un.sysname, size);
					break;
			case 'v':	strlcpy(ptr, un.version, size);
					break;
			case 'a':	snprintf(ptr, size, "%s %s %s %s %s",
						un.sysname, un.nodename,
						un.release, un.version,
						un.machine);
					break;
			case '%':	strlcpy(ptr, "%", size);
		    }
		    ptr += strlen(ptr);
		}
		else
		    *ptr++ = input[i];
	}
	*ptr = 0;

	RETURN_STR(tmp);
#endif
}

BUILT_IN_FUNCTION(function_querywin, args)
{
	Window *w = NULL;
	char *	nick = NULL;
	int	servref = -1;

	GET_STR_ARG(nick, args);
	if (args && *args)
		GET_INT_ARG(servref, args);

	while (traverse_all_windows(&w))
	    if (w->query_nick && !my_stricmp(w->query_nick, nick))
		if (servref < 0 || servref == w->server)
			RETURN_INT(w->refnum);

	RETURN_INT(-1);
}

BUILT_IN_FUNCTION(function_winrefs, args)
{
	Window *w = NULL;
	char *retval = NULL;
	size_t  rvclue=0;

	while (traverse_all_windows(&w))
		m_sc3cat(&retval, space, ltoa(w->refnum), &rvclue);

	RETURN_MSTR(retval);
}

/*
 * $mask(type address)      OR
 * $mask(address type)
 * Returns address with a mask specified by type.
 *
 * $mask(1 nick!khaled@mardam.demon.co.uk)  returns *!*khaled@mardam.demon.co.uk
 * $mask(2 nick!khaled@mardam.demon.co.uk)  returns *!*@mardam.demon.co.uk
 *
 * The available types are:
 *
 * 0: *!user@host.domain
 * 1: *!*user@host.domain
 * 2: *!*@host.domain
 * 3: *!*user@*.domain
 * 4: *!*@*.domain
 * 5: nick!user@host.domain
 * 6: nick!*user@host.domain
 * 7: nick!*@host.domain
 * 8: nick!*user@*.domain
 * 9: nick!*@*.domain
 * 10: *!*@<number-masked-host>.domain		(matt)
 * 11: *!*user@<number-masked-host>.domain	(matt)
 * 12: nick!*@<number-masked-host>.domain	(matt)
 * 13: nick!*user@<number-masked-host>.domain	(matt)
 */
BUILT_IN_FUNCTION(function_mask, args)
{
	char *	nuh;
	char *	nick = NULL;
	char *	user = NULL;
	char *	fqdn = NULL;
	char *	host = NULL;
	char *	domain = NULL;
	int   	which;
	char	stuff[BIG_BUFFER_SIZE + 1];
	int	ip = 0;
	char	*first_arg;
	char	*ptr;

	first_arg = new_next_arg(args, &args);
	if (is_number(first_arg))
	{
		which = my_atol(first_arg);
		nuh = args;
	}
	else
	{
		nuh = first_arg;
		GET_INT_ARG(which, args);
	}

	if (figure_out_address(nuh, &nick, &user, &fqdn))
		RETURN_EMPTY;
	if (strchr(fqdn, '.') == NULL) {
		domain = NULL;
		host = fqdn;
	}
	else if (figure_out_domain(fqdn, &host, &domain, &ip))
		RETURN_EMPTY;

	/*
	 * Deal with ~jnelson@acronet.net for example, and all sorts
	 * of other av2.9 breakage.
	 */
	if (strchr("~^-+=", *user))
		user++;

	/* Make sure 'user' isnt too long for a ban... */
	if (strlen(user) > 7)
	{
		user[7] = '*';
		user[8] = 0;
	}

	if (!host) host = empty_string;
	if (!domain) domain = empty_string;

	/* DOT gives you a "." if the host and domain are both non-empty */
#define USER (user == star ? empty_string : user)
#define DOT  (*domain && *host ? dot : empty_string)
#define MASK1(x, y) snprintf(stuff, BIG_BUFFER_SIZE, x, y); break;
#define MASK2(x, y, z) snprintf(stuff, BIG_BUFFER_SIZE, x, y, z); break;
#define MASK3(x, y, z, a) snprintf(stuff, BIG_BUFFER_SIZE, x, y, z, a); break;
#define MASK4(x, y, z, a, b) snprintf(stuff, BIG_BUFFER_SIZE, x, y, z, a, b); break;
#define MASK5(x, y, z, a, b, c) snprintf(stuff, BIG_BUFFER_SIZE, x, y, z, a, b, c); break;

	if (!ip) 
	switch (which)
	{
		case 0:  MASK4("*!%s@%s%s%s",         user, host, DOT, domain)
		case 1:  MASK4("*!*%s@%s%s%s",        USER, host, DOT, domain)
		case 2:  MASK3("*!*@%s%s%s",                host, DOT, domain)
		case 3:  MASK3("*!*%s@*%s%s",         USER,       DOT, domain)
		case 4:  MASK2("*!*@*%s%s",                       DOT, domain)
		case 5:  MASK5("%s!%s@%s%s%s",  nick, user, host, DOT, domain)
		case 6:  MASK5("%s!*%s@%s%s%s", nick, USER, host, DOT, domain)
		case 7:  MASK4("%s!*@%s%s%s",   nick,       host, DOT, domain)
		case 8:  MASK4("%s!*%s@*%s%s",  nick, USER,       DOT, domain)
		case 9:  MASK3("%s!*@*%s%s",    nick,             DOT, domain)
		case 10: mask_digits(&host);
			 MASK3("*!*@%s%s%s",                host, DOT, domain)
		case 11: mask_digits(&host);
			 MASK4("*!*%s@%s%s%s",        USER, host, DOT, domain)
		case 12: mask_digits(&host);
			 MASK4("%s!*@%s%s%s",   nick,       host, DOT, domain)
		case 13: mask_digits(&host);
			 MASK5("%s!*%s@%s%s%s", nick, USER, host, DOT, domain)
	}
	else /* in the case of IPs, we always have domain/host */
	switch (which)
	{
		case 0:  MASK3("*!%s@%s.%s",          user, domain, host)
		case 1:  MASK3("*!*%s@%s.%s",         USER, domain, host)
		case 2:  MASK2("*!*@%s.%s",                 domain, host)
		case 3:  MASK2("*!*%s@%s.*",          USER, domain)
		case 4:  MASK1("*!*@%s.*",                  domain)
		case 5:  MASK4("%s!%s@%s.%s",   nick, user, domain, host)
		case 6:  MASK4("%s!*%s@%s.%s",  nick, USER, domain, host)
		case 7:  MASK3("%s!*@%s.%s",    nick,       domain, host)
		case 8:  MASK3("%s!*%s@%s.*",   nick, USER, domain)
		case 9:  MASK2("%s!*@%s.*",     nick,       domain)
		case 10: MASK1("*!*@%s.*",                  domain)
		case 11: MASK2("*!*%s@%s.*",          USER, domain)
		case 12: MASK2("%s!*@%s.*",     nick,       domain)
		case 13: MASK3("%s!*%s@%s.*",   nick, USER, domain)
	}

	/* Clean up any non-printable chars */
	for (ptr = stuff; *ptr; ptr++)
		if (!isgraph(*ptr))
			*ptr = '?';

	RETURN_STR(stuff);
}

BUILT_IN_FUNCTION(function_ischanvoice, input)
{
	char	*nick;

	GET_STR_ARG(nick, input);
	RETURN_INT(is_chanvoice(input, nick));
}

BUILT_IN_FUNCTION(function_ishalfop, input)
{
	char	*nick;

	GET_STR_ARG(nick, input);
	RETURN_INT(is_halfop(input, nick));
}
BUILT_IN_FUNCTION(function_servports, input)
{
	int	servnum = from_server;

	if (*input)
		GET_INT_ARG(servnum, input);

	if (servnum == -1)
		servnum = from_server;
	if (servnum < 0 || servnum > number_of_servers)
		RETURN_EMPTY;

	return m_sprintf("%d %d", get_server_port(servnum),
				  get_server_local_port(servnum));
}

BUILT_IN_FUNCTION(function_chop, input)
{
	char *buffer;
	int howmany = 1;

	if (my_atol(input))
		GET_INT_ARG(howmany, input);

	buffer = m_strdup(input);
	chop(buffer, howmany);
	return buffer;
}

BUILT_IN_FUNCTION(function_winlevel, input)
{
	Window	*win;
	char	*desc;
	char *	retval;

	if (input && *input)
	{
		GET_STR_ARG(desc, input);
		win = get_window_by_desc(desc);
	}
	else
		win = current_window;

	if (!win)
		RETURN_EMPTY;

	retval = bits_to_lastlog_level(win->window_level);
	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_igtype, input)
{
	char *retval;
	retval = get_ignore_types_by_pattern(input);
	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_rigtype, input)
{
	char *retval;
	retval = get_ignore_patterns_by_type(input);
	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_getuid, input)
{
	RETURN_INT(getuid());
}

BUILT_IN_FUNCTION(function_getgid, input)
{
	RETURN_INT(getgid());
}

BUILT_IN_FUNCTION(function_getlogin, input)
{
#ifdef HAVE_GETLOGIN
	char *retval = getlogin();
#else
	char *retval = getenv("LOGNAME");
#endif
	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_getpgrp, input)
{
	RETURN_INT(getpgrp());
}

BUILT_IN_FUNCTION(function_iscurchan, input)
{
	char 	*chan;
	Window 	*w = NULL;

	GET_STR_ARG(chan, input);
	while (traverse_all_windows(&w))
	{
		/*
		 * Check to see if the channel specified is the current
		 * channel on *any* window for the current server.
		 */
		if (w->current_channel &&
			!my_stricmp(chan, w->current_channel) &&
			w->server == from_server)
				RETURN_INT(1);
	}

	RETURN_INT(0);
}

/* <horny> can you add something for me?  */
BUILT_IN_FUNCTION(function_channel, input)
{
	char *chan;

	chan = new_next_arg(input, &input);	/* dont use GET_STR_ARG */
	return scan_channel(chan);
}

BUILT_IN_FUNCTION(function_pad, input)
{
	int	width;
	size_t	awidth, len;
	char	*pads;
	char	*retval;

	GET_INT_ARG(width, input);
	GET_STR_ARG(pads, input);
	len = strlen(input);

	if (width > 0)
		awidth = width;
	else
		awidth = -width;

	if (awidth < len)
		RETURN_STR(input);

	retval = new_malloc(awidth + 2);
	return strformat(retval, input, width, (int)*pads);
}


/*
 * $remws(word word word / word word word)
 * Returns the right hand side unchanged, except that any word on the right
 * hand side that is also found on the left hand side will be removed.
 * Space is *not* retained.  So there.
 */
BUILT_IN_FUNCTION(function_remws, word)
{
	char    *left = NULL,
		**lhs = NULL,
		*right = NULL,
		**rhs = NULL, 
		*booya = NULL;
	int	leftc,
		rightc,
		righti;
	size_t	rvclue=0;

	left = word;
	if (!(right = strchr(word,'/')))
		RETURN_EMPTY;

	*right++ = 0;
	leftc = splitw(left, &lhs);
	qsort((void *)lhs, leftc, sizeof(char *), sort_it);
	rightc = splitw(right, &rhs);

	for (righti = 0; righti < rightc; righti++)
	{
		if (!bsearch(&rhs[righti], lhs, leftc, sizeof(char *), sort_it))
			m_sc3cat(&booya, space, rhs[righti], &rvclue);
	}

	new_free((char **)&lhs);
	new_free((char **)&rhs);

	RETURN_MSTR(booya);
}

BUILT_IN_FUNCTION(function_printlen, input)
{
	u_char *copy;
	int	retval;

	copy = normalize_string(input, 0);	/* Normalize string */
	retval = output_with_count(input, 0, 0);
	new_free(&copy);
	RETURN_INT(retval);
}

BUILT_IN_FUNCTION(function_stripansicodes, input)
{
        return normalize_string(input, 1);      /* This is ok now */
}

/*
 * $isnumber(number base)
 * returns the empty value if nothing is passed to it
 * returns 0 if the value passed is not a number
 * returns 1 if the value passed is a number.
 *
 * The "base" number is optional and should be prefixed by the 
 * 	character 'b'.  ala,   $isnumber(0x0F b16) for hexadecimal.
 *	the special base zero (b0) means to 'auto-detect'.  Base must
 *	be between 0 and 36, inclusive.  If not, it defaults to 0.
 */
BUILT_IN_FUNCTION(function_isnumber, input)
{
	int	base = 0;
	char	*endptr;
	char	*barg;
	char	*number = NULL;

	/*
	 * See if the first arg is the base
	 */
	barg = new_next_arg(input, &input);

	/*
	 * If it is, the number is the 2nd arg
	 */
	if (barg && *barg == 'b' && is_number(barg + 1))
	{
		GET_STR_ARG(number, input);
	}
	/*
	 * Otherwise, the first arg is the number,
	 * the 2nd arg probably is the base
	 */
	else
	{
		number = barg;
		barg = new_next_arg(input, &input);
	}

	/*
	 * If the user specified a base, parse it.
	 * Must be between 0 and 36.
	 */
	if (barg && *barg == 'b')
	{
		base = my_atol(barg + 1);
		if (base < 0 || base > 36)
			base = 0;
	}

	/*
	 * Must have specified a number, though.
	 */
	RETURN_IF_EMPTY(number);

	strtol(number, &endptr, base);
	if (*endptr == '.')
	{
		if (base == 0)
			base = 10;		/* XXX So i'm chicken. */
		strtol(endptr + 1, &endptr, base);
	}

	if (*endptr)
		RETURN_INT(0);
	else
		RETURN_INT(1);
}

/*
 * $rest(index string)
 * Returns 'string' starting at the 'index'th character
 * Just like $restw() does for words.
 */
BUILT_IN_FUNCTION(function_rest, input)
{
	int	start = 1;
	char *	test_input;

	/*
	 * XXX - This is a total hack.  I know it.
	 */
	test_input = input;
	parse_number(&test_input);
	if (test_input > input && my_isspace(*test_input))
		GET_INT_ARG(start, input);

	if (start <= 0)
		RETURN_STR(input);

	if (start >= strlen(input))
		RETURN_EMPTY;

	RETURN_STR(input + start);
}


/* Written by panasync */
BUILT_IN_FUNCTION(function_getsets, input)
{
extern 	char 	*get_set (char *);
	char 	*s = NULL;
	char	*ret = NULL;
	size_t	clue = 0;

	if (!input || !*input)
		return get_set(NULL);

	while ((s = next_arg(input, &input)))
	{
		char *s2 = get_set(s);
		m_sc3cat(&ret, space, s2, &clue);
		new_free(&s2);
	}

	RETURN_MSTR(ret);
}

BUILT_IN_FUNCTION(function_getcommands, input)
{
	char	*s = NULL;
	char	*ret = NULL;
	size_t	clue = 0;

	if (!input || !*input)
		return get_command(NULL);

	while ((s = next_arg(input, &input)))
	{
		char *s2 = get_command(s);
		m_sc3cat(&ret, space, s2, &clue);
		new_free(&s2);
	}

	RETURN_MSTR(ret);
}

/* Written by nutbar */
BUILT_IN_FUNCTION(function_servernum, input)
{
	int 	sval;
	char 	*which;
	const char *s;

	/* 
	 * Return current server refnum if no input given
	 */
	if (!*input)
		RETURN_INT(from_server);

	GET_STR_ARG(which, input);

	/*
	 * Find the matching server name from the list
	 */
	for (sval = 0; sval < number_of_servers; sval++) 
	{
		/*
		 * Try and match to what the server thinks its name is
		 */
		if ((s = get_server_itsname(sval)) && !my_stricmp(which, s))
			RETURN_INT(sval);

		/*
		 * Otherwise, try and match what we think its name is
		 */
		else if (!my_stricmp(which, get_server_name(sval)))
			RETURN_INT(sval);
	}

	/* Ok. i give up, return -1. */
	RETURN_INT(-1);
}


BUILT_IN_FUNCTION(function_stripc, input)
{
	char	*retval;

	retval = alloca(strlen(input) + 1);
	strcpy_nocolorcodes(retval, input);
	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_stripcrap, input)
{
	char	*how;
	int	mangle;
	char	*output;
	size_t	size;

	GET_STR_ARG(how, input);
	mangle = parse_mangle(how, 0, NULL);

	size = strlen(input) * 3 + 1;
	output = new_malloc(size);
	/* Obviously this is safe. */
	strcpy(output, input);
	if (mangle_line(output, mangle, size) > size)
		;		/* Result has been truncated. ick. */
	return output;			/* DONT MALLOC THIS */
}

/*
 * Date: Wed, 2 Sep 1998 18:20:34 -0500 (CDT)
 * From: CrackBaby <crack@feeding.frenzy.com>
 */
/* 
 * $getopt(<optopt var> <optarg var> <opt string> <argument list>)
 *
 * Processes a list of switches and args.  Returns one switch char each time
 * it's called, sets $<optopt var> to the char, and sets $<optarg var> to the
 * value of the next argument if the switch needs one.
 *
 * Syntax for <opt string> and <argument list> should be identical to
 * getopt(3).  A ':' in <opt string> is a required argument, and a "::" is an
 * optional one.  A '-' by itself in <argument list> is a null argument, and
 * switch parsing stops after a "--"
 *
 * If a switch requires an argument, but the argument is missing, $getopt()
 * returns a '-'  If a switch is given which isn't in the opt string, the
 * function returns a '!'  $<optopt var> is still set in both cases.
 *
 * Example usage:
 * while (option = getopt(optopt optarg "ab:c:" $*)) {
 *	switch ($option) {
 * 		(a) {echo * option "$optopt" used}
 * 		(b) {echo * option "$optopt" used - $optarg}
 * 		(c) {echo * option "$optopt" used - $optarg}
 * 		(!) {echo * option "$optopt" is an invalid option}
 * 		(-) {echo * option "$optopt" is missing an argument}
 *	}
 * }
 */
BUILT_IN_FUNCTION(function_getopt, input)
{
static	char	*switches = NULL,
		*args = NULL,
		*last_input = NULL,
		*swptr = NULL,
		*aptr = NULL;
	char	*optopt_var, 
		*optarg_var,
		*optstr,
		*optptr;
	char	*extra_args = NULL;
	int	arg_flag = 0;
	int	new_input = 0;
	char	tmpstr[2];

	/* 
	 * If this is not the same argument set as last time, re-initialize
	 * the state variables to the new input data.
	 */
	if (last_input == NULL || strcmp(last_input, input)) 
	{
		size_t	input_size;

		input_size = strlen(input);
		malloc_strcpy(&last_input, input);

		new_realloc((void **)&switches, input_size * 2 + 1);
		*switches = 0;
		swptr = switches;

		new_realloc((void **)&args, input_size + 1);
		*args = 0;
		aptr = args;

		extra_args = (char *)alloca(input_size * 2 + 1);
		*extra_args = 0;

		new_input = 1;
	}

	/* Three arguments are required -- two variables and an op-string */
	GET_STR_ARG(optopt_var, input); 
	GET_STR_ARG(optarg_var, input); 
	GET_STR_ARG(optstr, input);
	if (!optopt_var || !optarg_var || !optstr)
		RETURN_EMPTY;

	if (new_input)
	{
	    char *	tmp;

	    /* Process each word in the input string */
	    while ((tmp = new_next_arg(input, &input)))
	    {
		/* Word is a switch or a group of switches */
		if (tmp[0] == '-' && 
		    tmp[1] && tmp[1] != '-' &&
		    arg_flag == 0)
		{
		    /* Look at each char after the '-' */
		    for (++tmp; *tmp; tmp++)
		    {
			/* 
			 * If the char is found in optstr and doesn't need 
			 * an argument, it's added to the switches list.
		   	 * switches are stored as "xy" where x is the switch 
			 * letter and y is:
			 *	'_' normal switch
			 *	':' switch with arg
			 *	'-' switch with missing arg
			 *	'!' unrecognized switch
			 */
			*swptr++ = *tmp;

			/* char is a valid switch */
			if ((optptr = strchr(optstr, *tmp)))
			{
			    /* char requires an argument */
			    if (optptr[1] == ':')
			    {
				/* -xfoo, argument is "foo" */
				if (tmp[1])
				{
					tmp++;
					strcat(args, tmp);
					strcat(args, " ");
					*swptr++ = ':';
					break;
				}
				/* 
				 * Otherwise note that the next word in 
				 * the input is our arg. 
				 */
				else if (*(optptr + 2) == ':')
					arg_flag = 2;
				else
					arg_flag = 1;
			    }
			    /* Switch needs no argument */
			    else 
				*swptr++ = '_';
			}
			/* Switch is not recognized */
			else 
			    *swptr++ = '!';
		    }
		}
		else
		{
		    /* Everything after a "--" is added to extra_args */
		    if (*tmp == '-' && tmp[1] == '-')
		    {
			tmp += 2;
			strcat(extra_args, tmp);
			strcat(extra_args, input);
			*input = 0;
		    }

		    /* A '-' by itself is a null arg  */
		    else if (*tmp == '-' && !*(tmp + 1))
		    {
			if (arg_flag == 1)
				*swptr++ = '-';
			else if (arg_flag == 2)
				*swptr++ = '_';
			*tmp = 0;
			arg_flag = 0;
		    }

		    /* 
		     * If the word doesn't start with a '-,' it must be
		     * either the argument of a switch or just extra info. 
		     */
		    else if (arg_flag)
		    {
			/* 
			 * If arg_flag is positive, we've processes
			 * a switch which requires an arg and we can
			 * just tack the word onto the end of args[] 
			 */
			strcat(args, tmp);
			strcat(args, " ");
			*swptr++ = ':';
			arg_flag = 0;
		    }
		    else
		    {
			/* 
			 * If not, we'll put it aside and add it to
			 * args[] after all the switches have been
			 * looked at. 
			 */
			strcat(extra_args, tmp);
			strcat(extra_args, " ");
		    }
		}
	    }

	    /* 
	     * If we're expecting an argument to a switch, but we've
	     * reached the end of our input, the switch is missing its
	     * arg. 
	     */
	    if (arg_flag == 1)
		*swptr++ = '-';
	    else if (arg_flag == 2)
		*swptr++ = '_';
	    strcat(args, extra_args);

	    *swptr = 0;
	    swptr = switches;
	}

	/* Terminating condition */
	if (*swptr == 0)
	{
		add_var_alias(optopt_var, NULL, 0);
		add_var_alias(optarg_var, aptr, 0);

		*switches = 0;
		*args = 0;
		swptr = switches;
		aptr = args;
		new_free(&last_input);		/* Reset the system */

		RETURN_EMPTY;
	}

	tmpstr[0] = *swptr++;
	tmpstr[1] = 0;

	switch (*swptr++)
	{
		case '_':	
			add_var_alias(optopt_var, tmpstr, 0);
			add_var_alias(optarg_var, NULL, 0);
			RETURN_STR(tmpstr);
		case ':':
			add_var_alias(optopt_var, tmpstr, 0);
			add_var_alias(optarg_var, next_arg(aptr, &aptr), 0);
			RETURN_STR(tmpstr);
		case '-':
			add_var_alias(optopt_var, tmpstr, 0);
			add_var_alias(optarg_var, NULL, 0);
			RETURN_STR("-");
		case '!':
			add_var_alias(optopt_var, tmpstr, 0);
			add_var_alias(optarg_var, NULL, 0);
			RETURN_STR("!");
		default:
			/* This shouldn't happen */
			yell("*** getopt switch broken: %s", tmpstr);
			RETURN_EMPTY;
	}
}

BUILT_IN_FUNCTION(function_isaway, input)
{
        int     refnum = -1;

        if (!*input)
                refnum = from_server;
        else
                GET_INT_ARG(refnum, input);

        if (get_server_away(refnum))
                RETURN_INT(1);

        RETURN_INT(0);
}


/* 
 * KnghtBrd requested this. Returns the length of the longest word
 * in the word list.
 */
BUILT_IN_FUNCTION(function_maxlen, input)
{
	size_t	maxlen = 0;
	size_t	len;
	char	*arg;

	while (input && *input)
	{
		GET_STR_ARG(arg, input)

		if ((len = strlen(arg)) > maxlen)
			maxlen = len;
	}

	RETURN_INT(maxlen);
}


/*
 * KnghtBrd requested this.  It returns a string that is the leading
 * substring of ALL the words in the word list.   If no string is common
 * to all words in the word list, then the FALSE value is returned.
 *
 * I didn't give this a whole lot of thought about the optimal way to
 * do this, so if you have a better idea, let me know.  All i do here is
 * start at 1 character and walk my way longer and longer until i find a 
 * length that is not common, and then i stop there.  So the expense of this
 * algorithm is (M*N) where M is the number of words and N is the number of
 * characters they have in common when we're all done.
 */
BUILT_IN_FUNCTION(function_prefix, input)
{
	char	**words = NULL;
	int	numwords;
	int	max_len;
	int	len_index;
	int	word_index;
	char	*retval = NULL;

	RETURN_IF_EMPTY(input);

	numwords = splitw(input, &words);
	if (numwords == 0)
		RETURN_EMPTY;		/* "Oh bother," said pooh. */

	max_len = strlen(words[0]);

	for (len_index = 1; len_index <= max_len; len_index++)
	{
	    for (word_index = 1; word_index < numwords; word_index++)
	    {
		/*
		 * Basically, our stopping point is the first time we 
		 * see a string that does NOT share the first "len_index"
		 * characters with words[0] (which is chosen arbitrarily).
		 * As long as all words start with the same leading chars,
		 * we march along trying longer and longer substrings,
		 * until one of them doesnt work, and then we exit right
		 * there.
		 */
		if (my_strnicmp(words[0], words[word_index], len_index))
		{
			retval = new_malloc(len_index + 1);
			strmcpy(retval, words[0], len_index - 1);
			new_free((char **)&words);
			return retval;
		}
	    }
	}

	retval = m_strdup(words[0]);
	new_free((char **)&words);
	return retval;
}

/*
 * I added this so |rain| would stop asking me for it. ;-)  I reserve the
 * right to change the implementation of this in the future. 
 */
BUILT_IN_FUNCTION(function_functioncall, input)
{
	/*
	 * These two variables are supposed to be private inside alias.c
	 * Even though i export them here, it is EVIL EVIL EVIL to use
	 * them outside of alias.c.  So do not follow my example and extern
	 * these for your own use -- because who knows, one day i may make
	 * them static and your use of them will not be my fault! :p
	 */
	extern	int	wind_index;		 	/* XXXXX Ick */
	extern	int	last_function_call_level;	/* XXXXX Ick */

	/*
	 * If the last place we slapped down a FUNCTION_RETURN is in the
	 * current operating stack frame, then we're in a user-function
	 * call.  Otherwise we're not.  Pretty simple.
	 */
	if (last_function_call_level == wind_index)
		RETURN_INT(1);
	else
		RETURN_INT(0);
}

/*
 * Creates a 32 bit hash value for a specified string
 * Usage: $hash_32bit(word length)
 *
 * "word" is the value to be hashed, and "length" is the number
 * of characters to hash.  If "length" is omitted or not 0 < length <= 64,
 * then it defaults to 20.
 *
 * This was contributed by srfrog (srfrog@lice.com).  I make no claims about
 * the usefulness of this hashing algorithm.
 *
 * The name was chosen just in case we decided to implement other hashing
 * algorithms for different sizes or types of return values.
 */
BUILT_IN_FUNCTION(function_hash_32bit, input)
{
	char *		u_word;
	char *		word;
	char		c;
	unsigned	bit_hi = 0;
	int		bit_low = 0;
	int		h_val;
	int		len;

	GET_STR_ARG(word, input)
	len = my_atol(input);

	if (len <= 0 || len > 64)
		len = 20;

	for (u_word = word; *u_word && len > 0; ++u_word, --len)
	{
		c = tolower(*u_word);
		bit_hi = (bit_hi << 1) + c;
		bit_low = (bit_low >> 1) + c;
	}
	h_val = ((bit_hi & 8191) << 3) + (bit_low & 0x7);
	RETURN_INT(h_val);
}


/*
 * Usage: $indextoword(position text)
 *
 * This function returns the word-number (counting from 0) that contains
 * the 'position'th character in 'text' (counting from 0), such that the
 * following expression:
 *
 *		$word($indextoword($index(. $*) $*) $*)
 *
 * would return the *entire word* in which the first '.' in $* is found.
 * If 'position' is -1, or past the end of the string, -1 is returned as
 * an error flag.  Note that this function can be used anywhere that a
 * word-number would be used, such as $chngw().  The empty value is returned
 * if a syntax error returns.
 *
 * DONT ASK ME to support 'position' less than 0 to indicate a position
 * from the end of the string.  Either that can be supported, or you can 
 * directly use $index() as the first argument; you can't do both.  I chose
 * the latter intentionally.  If you really want to calculate from the end of
 * your string, then just add your negative value to $strlen(string) and
 * pass that.
 */
BUILT_IN_FUNCTION(function_indextoword, input)
{
	size_t	pos;
	size_t	len;

	GET_INT_ARG(pos, input);
	if (pos < 0)
		RETURN_EMPTY;
	len = strlen(input);
	if (pos < 0 || pos >= len)
		RETURN_EMPTY;

	/* 
	 * XXX
	 * Using 'word_count' to do this is a really lazy cop-out, but it
	 * renders the desired effect and its pretty cheap.  Anyone want
	 * to bicker with me about it?
	 */
	/* Truncate the string if neccesary */
	if (pos + 1 < len)
		input[pos + 1] = 0;
	RETURN_INT(word_count(input) - 1);
}

BUILT_IN_FUNCTION(function_realpath, input)
{
	char	resolvedname[MAXPATHLEN];
	char	*retval;

#ifdef HAVE_REALPATH
	if ((retval = realpath(input, resolvedname)))
		RETURN_STR(resolvedname);
	else
		RETURN_EMPTY;
#else
	RETURN_STR(input);
#endif
}

BUILT_IN_FUNCTION(function_ttyname, input)
{
	char *retval = ttyname(0);
	RETURN_STR(retval);
}

/* 
 * $insert(num word string of text)
 * returns "string of text" such that "word" begins in the "num"th position
 * in the string ($index()-wise)
 * NOTE: Positions are numbered from 0
 * EX: $insert(3 baz foobarbooya) returns "foobazbarbooya"
 */
BUILT_IN_FUNCTION(function_insert, word)
{
	int	where;
	char *	inserted;
	char *	result;

	GET_INT_ARG(where, word);
	GET_STR_ARG(inserted, word);

	if (where <= 0)
		result = m_strdup(empty_string);
	else
		result = strext(word, word + where);

	m_3cat(&result, inserted, word + where);
	return result;				/* DONT USE RETURN_STR HERE! */
}

/* Submitted by srfrog, August 11, 1999 */
BUILT_IN_FUNCTION(function_urlencode, input)
{
	char *retval = urlencode(input);
	RETURN_STR(retval);
}

/* Submitted by srfrog, August 11, 1999 */
BUILT_IN_FUNCTION(function_urldecode, input)
{
	char *retval = urldecode(input, NULL);
	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_stat, input)
{
	char *		filename;
	char 		retval[BIG_BUFFER_SIZE];
	struct stat	sb;

	GET_STR_ARG(filename, input);
	if (stat(filename, &sb) < 0)
		RETURN_EMPTY;

	snprintf(retval, BIG_BUFFER_SIZE,
		"%d %d %o %d %d %d %d %lu %lu %lu %ld %ld %ld",
		(int)	sb.st_dev,		/* device number */
		(int)	sb.st_ino,		/* Inode number */
		(int)	sb.st_mode,		/* Permissions */
		(int)	sb.st_nlink,		/* Hard links */
		(int)	sb.st_uid,		/* Owner UID */
		(int)	sb.st_gid,		/* Owner GID */
		(int)	sb.st_rdev,		/* Device type */
		(u_long)sb.st_size,		/* Size of file */
		(u_long)sb.st_blksize,		/* Size of each block */
		(u_long)sb.st_blocks,		/* Blocks used in file */
		(long)	sb.st_atime,		/* Last-access time */
		(long)	sb.st_mtime,		/* Last-modified time */
		(long)	sb.st_ctime		/* Last-change time */
			);

	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_isdisplaying, input)
{
	RETURN_INT(window_display);
}

BUILT_IN_FUNCTION(function_getcap, input)
{
	char *	type;

	GET_STR_ARG(type, input);
	if (!my_stricmp(type, "TERM"))
	{
		char *	retval;
		char *	term = NULL;
		int	querytype = 0;
		int	mangle = 1;
		
		GET_STR_ARG(term, input);
		if (*input)
			GET_INT_ARG(querytype, input);
		if (*input)
			GET_INT_ARG(mangle, input);

		if (!term)			/* This seems spurious */
			RETURN_EMPTY;
		
		if ((retval = get_term_capability(term, querytype, mangle)))
			RETURN_STR(retval);

		RETURN_EMPTY;
	}

	RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_getset, input)
{
	char *retval = make_string_var(input);
	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_builtin, input)
{
	char *retval = built_in_alias(*input, NULL);
	RETURN_MSTR(retval);
}

/*
 * Date: Fri, 14 Jan 2000 00:48:55 -0500
 * Author: IceKarma (ankh@canuck.gen.nz)
 * Contributed by: author
 *
 * Usage: $winscreen(#channel <server refnum|server name>)
 * Given a channel name and either a server refnum or a direct server
 * name or an effective server name, this function will return the
 * refnum of the window where the channel is the current channel (on that
 * server if appropriate)
 *
 * Returns -1 (Too few arguments specified or Window Not Found, or Window
 * is Hidden) on error
 */
BUILT_IN_FUNCTION(function_winscreen, input)
{
	Window *	foo = NULL;
	char *		stuff;
	int		num;

	if (!(stuff = new_next_arg(input, &input)))
		RETURN_INT(-1);

	if ((num = my_atol(stuff)))
		foo = get_window_by_refnum(num);
	if (!foo)
		foo = get_window_by_name(stuff);
	if (!foo || !foo->screen)
		RETURN_INT(-1);

	RETURN_INT(foo->screen->screennum);
}

/*
 * Returns what the status-line %F expando would return, except it is 
 * space-seperated.  Basically, it is all the windows that are hidden,
 * that have /window notify on, and have output since they were hidden.
 */
BUILT_IN_FUNCTION(function_notifywindows, input)
{
	char *	retval = NULL;
	size_t	rvclue=0;
	Window *window;

	window = NULL;
	while (traverse_all_windows(&window))
		if (window->miscflags & WINDOW_NOTIFIED)
			m_sc3cat(&retval, space, ltoa(window->refnum), &rvclue);

	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_loadinfo, input)
{
	return m_sprintf("%d %s", current_line(), current_filename());
}

BUILT_IN_FUNCTION(function_wordtoindex, input)
{
	int		wordnum;
	const char *	ptr;

	GET_INT_ARG(wordnum, input);
	move_to_abs_word(input, &ptr, wordnum);
	RETURN_INT((int)(ptr - input));
}

/*
 * Date: Tue, 25 Apr 2000 20:30:15 -0400
 * Author: IceKarma (ankh@canuck.gen.nz)
 * Contributed by: author
 *
 * Usage: $winsbsize() or $winsbsize(<window refnum or name>)
 * Given a window refnum or window name, this function will return the
 * number of lines available in the scrollback buffer.  Defaults to the
 * current window if none specified.
 *
 * Returns -1 (Window Not Found) on error
 */
BUILT_IN_FUNCTION(function_winsbsize, input)
{
	Window *winp;

	if (!(winp = *input ? get_window_by_desc(input) : current_window))
		RETURN_INT(-1);

	RETURN_INT(winp->display_buffer_size - 1);
}

/*
 * Date: Tue, 25 Apr 2000 20:40:18 -0400
 * Author: IceKarma (ankh@canuck.gen.nz)
 * Contributed by: author
 *
 * Usage: $winstatsize() or $winstatsize(<window refnum or name>)
 * Given a window refnum or window name, this function will return the
 * height of the window's status bar.  Defaults to the current window if
 * none specified.
 *
 * Returns -1 (Window Not Found) on error
 */
BUILT_IN_FUNCTION(function_winstatsize, input)
{
	Window *winp;

	if (!(winp = *input ? get_window_by_desc(input) : current_window))
		RETURN_INT(-1);
	RETURN_INT(winp->status.double_status ? 2 : 1);
}

/*
 * Date: Tue, 25 Apr 2000 20:46:25 -0400
 * Author: IceKarma (ankh@canuck.gen.nz)
 * Contributed by: author
 *
 * Usage: $wincurline() or $wincurline(<window refnum or name>)
 * Given a window refnum or window name, this function will return the
 * line number within the window the cursor is on.  Defaults to the
 * current window if none specified.
 *
 * Returns -1 (Window Not Found) on error
 */
BUILT_IN_FUNCTION(function_wincurline, input)
{
	Window *winp;

	if (!(winp = *input ? get_window_by_desc(input) : current_window))
		RETURN_INT(-1);
	RETURN_INT(winp->cursor);
}

/*
 * These four functions contributed by 
 * B. Thomas Frazier (tfrazier@mjolnir.gisystems.net)
 * On December 12, 2000.
 */

BUILT_IN_FUNCTION(function_iptolong, word)
{
	char *	dotted_quad;
	struct in_addr	addr;

	GET_STR_ARG(dotted_quad, word);
	if (inet_aton(dotted_quad, &addr))
		return m_sprintf("%lu", (unsigned long)ntohl(addr.s_addr));
	RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_longtoip, word)
{
	char *	ip32;
	struct in_addr	addr;

	GET_STR_ARG(ip32, word);
	addr.s_addr = (unsigned long)ntohl(strtoul(ip32, NULL, 10));
	RETURN_STR(inet_ntoa(addr));
}

BUILT_IN_FUNCTION(function_isencrypted, input)
{
	int	sval = from_server;

	if (*input)
		GET_INT_ARG(sval, input);

	/* garbage in, garbage out. */
	if (sval < 0 || sval >= number_of_servers)
		RETURN_INT(0);

	/* Check if it is encrypted connection */
	RETURN_INT(get_server_isssl(sval));
}

BUILT_IN_FUNCTION(function_ssl, words)
{
#ifdef HAVE_SSL
	RETURN_INT(1);
#else
	RETURN_INT(0);
#endif
}

BUILT_IN_FUNCTION(function_cipher, input)
{
#ifdef HAVE_SSL
	int     sval = from_server;

	if (*input)
		GET_INT_ARG(sval, input);

	if (sval < 0 || sval >= number_of_servers)
		RETURN_STR(NULL);

	RETURN_STR(get_server_cipher(sval));
#else
	RETURN_EMPTY;
#endif
}

#define MATH_RETVAL(x)						\
	{							\
		if (errno == 0) 				\
			RETURN_FLOAT(x);			\
		else if (errno == EDOM) 			\
			RETURN_STR("DOM");			\
		else if (errno == ERANGE) 			\
			RETURN_STR("RANGE");			\
		else						\
			RETURN_EMPTY;				\
	}

#define MATH_FUNCTION(x, y) 					\
	BUILT_IN_FUNCTION( x , word) 				\
	{ 							\
		double	num; 					\
								\
		GET_FLOAT_ARG(num, word); 			\
		errno = 0; 					\
		num = y (num); 					\
		MATH_RETVAL(num)				\
	}

#define MATH_FUNCTION2(x, y) 					\
	BUILT_IN_FUNCTION( x , word) 				\
	{ 							\
		int	level;					\
		double	num;					\
								\
		errno = 0;					\
		GET_INT_ARG(level, word);			\
		GET_FLOAT_ARG(num, word);			\
		num = y (level, num);				\
		MATH_RETVAL(num)				\
	}


MATH_FUNCTION(function_abs, fabs);
MATH_FUNCTION(function_ceil, ceil);
MATH_FUNCTION(function_floor, floor);

MATH_FUNCTION(function_exp, exp);
MATH_FUNCTION(function_log, log);
MATH_FUNCTION(function_log10, log10);

MATH_FUNCTION(function_cosh, cosh);
MATH_FUNCTION(function_sinh, sinh);
MATH_FUNCTION(function_tanh, tanh);
MATH_FUNCTION(function_acosh, acosh);
MATH_FUNCTION(function_asinh, asinh);
MATH_FUNCTION(function_atanh, atanh);

MATH_FUNCTION(function_cos, cos);
MATH_FUNCTION(function_sin, sin);
MATH_FUNCTION(function_tan, tan);
MATH_FUNCTION(function_acos, acos);
MATH_FUNCTION(function_asin, asin);

MATH_FUNCTION2(function_jn, jn);
MATH_FUNCTION2(function_yn, yn);

BUILT_IN_FUNCTION(function_atan, word)
{
	double	num, num1, num2;

	errno = 0;
	GET_FLOAT_ARG(num1, word);
	if (word && *word) 
	{
		GET_FLOAT_ARG(num2, word);
		num = atan2(num1, num2);
	} 
	else 
		num = atan(num1);

	MATH_RETVAL(num)
}

#ifdef PERL

BUILT_IN_FUNCTION(function_perl, input)
{
	extern char* perleval ( const char* );
	return perleval ( input );
}

BUILT_IN_FUNCTION(function_perlcall, input)
{
	char *sub=NULL;
	extern char* perlcall ( const char*, char*, char*, long, char* );
	GET_STR_ARG(sub, input);
	return perlcall ( sub, NULL, NULL, -1, input );
}

BUILT_IN_FUNCTION(function_perlxcall, input)
{
	long item=0;
	char *sub=NULL, *in=NULL, *out=NULL;
	extern char* perlcall ( const char*, char*, char*, long, char* );
	GET_STR_ARG(sub, input);
	if (input && *input) GET_STR_ARG(in, input);
	if (input && *input) GET_STR_ARG(out, input);
	if (input && *input) GET_INT_ARG(item, input);
	return perlcall ( sub, in, out, item, input );
}

#endif

BUILT_IN_FUNCTION(function_unsplit, input)
{
	char *	sep;
	char *	word;
	char *	retval = NULL;

	GET_STR_ARG(sep, input);
	while ((word = new_next_arg(input, &input)))
		m_s3cat(&retval, sep, word);
	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_encryptparm, input)
{
	char	*ret = NULL, *entry = NULL;
	size_t	clue = 0;
	Crypt	*key;

	GET_STR_ARG(entry, input);
	if ((key = is_crypted(entry))) 
	{
		m_sc3cat  (&ret, " ", key->nick, &clue);
		m_sc3cat_s(&ret, " ", key->key, &clue);
		m_sc3cat_s(&ret, " ", key->prog, &clue);
	}

	RETURN_MSTR(ret);
}
