/*
 * functions.c -- Built-in functions for ircII
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2010 EPIC Software Labs
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
/*
 * Some of the "others" include:
 *	Matt Carothers 			Colten Edwards
 *	James Sneeringer		Eli Sand
 */
/*
 * I split alias.c because it was just getting out of hand.
 */

#include "irc.h"
#define __need_ArgList_t__
#include "alias.h"
#include "alist.h"
#include "array.h"
#include "clock.h"
#include "ctcp.h"
#include "dcc.h"
#include "debug.h"
#include "commands.h"
#include "exec.h"
#include "files.h"
#include "flood.h"
#include "ignore.h"
#include "input.h"
#include "ircaux.h"
#include "keys.h"
#include "log.h"
#include "names.h"
#include "output.h"
#include "parse.h"
#include "screen.h"
#include "server.h"
#include "status.h"
#include "vars.h"
#include "window.h"
#include "termx.h"
#include "notify.h"
#include "numbers.h"
#include "sedcrypt.h"
#include "timer.h"
#define need_static_functions
#include "functions.h"
#include "options.h"
#include "words.h"
#include "reg.h"
#include "ifcmd.h"
#include "ssl.h"
#include "levels.h"
#include "extlang.h"
#include "ctcp.h"
#include "cJSON.h"

#ifdef NEED_GLOB
# include "glob.h"
#else
# include <glob.h>
#endif

#ifdef HAVE_REGEX_H
# include <regex.h>
#endif
#ifdef HAVE_UNAME
# include <sys/utsname.h>
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
	const char *	name;
	char *		(*func) (void);
}	BuiltIns;

static	BuiltIns built_in[] =
{
	{ ".",		alias_sent_nick 	},
	{ ",",		alias_recv_nick 	},
	{ ":",		alias_joined_nick 	},
	{ ";",		alias_public_nick 	},
	{ "$",		alias_dollar 		},
	{ "A",		alias_away 		},
	{ "B",		alias_msg_body 		},
	{ "C",		alias_channel 		},
	{ "D",		alias_detected 		},
	{ "E",		alias_idle 		},
	{ "F",		alias_online 		},
	{ "G",		alias_banner		},
	{ "H", 		alias_current_numeric 	},
	{ "I",		alias_invite 		},
	{ "J",		alias_version_str 	},
	{ "K",		alias_cmdchar 		},
	{ "L",		alias_line 		},
	{ "M",		alias_modes 		},
	{ "N",		alias_nick 		},
	{ "O",		alias_oper 		},
	{ "P",		alias_chanop 		},
	{ "Q",		alias_query_nick 	},
	{ "R",		alias_server_version 	},
	{ "S",		alias_server 		},
	{ "T",		alias_target 		},
	{ "U",		alias_buffer 		},
	{ "V",		alias_version 		},
	{ "W",		alias_currdir 		},
	{ "X",		alias_show_userhost 	},
	{ "Y",		alias_show_realname 	},
	{ "Z",		alias_time 		},
	{ 0,	 	NULL 			}
};

/* the 30 "standard" functions */
static	char
	*function_channels 	(char *), 
	*function_connect 	(char *),
	*function_curpos 	(char *), 
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
	*function_asciiq 	(char *),
	*function_before 	(char *),
	*function_beforew 	(char *),
	*function_bindctl	(char *),
	*function_builtin	(char *),
	*function_ceil		(char *),
	*function_center 	(char *),
	*function_cexist	(char *),
	*function_chankey	(char *),
	*function_channel	(char *),
	*function_channellimit	(char *),
	*function_channelmode	(char *),
	*function_channelsyncing (char *),
	*function_check_code	(char *),
	*function_chmod		(char *),
	*function_chngw 	(char *),
	*function_chop		(char *),
	*function_chops 	(char *),
	*function_chr 		(char *),
	*function_chrq 		(char *),
	*function_cipher	(char *),
	*function_close 	(char *),
	*function_cofilter	(char *),
	*function_corfilter	(char *),
	*function_common 	(char *),
	*function_convert 	(char *),
	*function_copattern 	(char *),
	*function_corpattern 	(char *),
	*function_cos		(char *),
	*function_cosh		(char *),
	*function_count		(char *),
	*function_crypt 	(char *),
	*function_curcmd	(char *),
	*function_currchans	(char *),
	*function_dbmctl	(char *),
	*function_dccctl	(char *),
	*function_deuhc		(char *),
	*function_diff 		(char *),
	*function_encryptparm 	(char *),
	*function_eof 		(char *),
	*function_epic		(char *),
	*function_error		(char *),
	*function_exec		(char *),
	*function_execctl	(char *),
	*function_exp		(char *),
	*function_fnexist	(char *),
	*function_fexist 	(char *),
	*function_filter 	(char *),
	*function_findw		(char *),
	*function_findws	(char *),
	*function_fix_arglist	(char *),
        *function_fix_width     (char *),
	*function_floor		(char *),
	*function_fromw 	(char *),
	*function_fsize	 	(char *),
	*function_ftime		(char *),
	*function_ftruncate 	(char *),
	*function_functioncall	(char *),
	*function_geom		(char *),
	*function_getcap	(char *),
	*function_getcommands	(char *),
	*function_getenv	(char *),
	*function_getfunctions	(char *),
	*function_getgid	(char *),
	*function_getlogin	(char *),
	*function_getopt	(char *),
	*function_getpgrp	(char *),
	*function_getserial	(char *),
	*function_getset	(char *),
	*function_getsets	(char *),
	*function_getuid	(char *),
	*function_glob		(char *),
	*function_globi		(char *),
	*function_hash_32bit	(char *),
#if 0
	*function_help_topics	(char *),
#endif
	*function_hookctl	(char *),
	*function_iconvctl	(char *),
	*function_idle		(char *),
	*function_ignorectl	(char *),
	*function_indextoword	(char *),
	*function_info		(char *),
	*function_insert 	(char *),
	*function_insertw 	(char *),
	*function_iptolong	(char *),
	*function_iptoname 	(char *),
	*function_irclib	(char *),
	*function_is8bit 	(char *),
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
	*function_isutf8	(char *),
	*function_jn		(char *),
	*function_joinstr	(char *),
	*function_jot 		(char *),
	*function_json_error	(char *),
	*function_json_explode	(char *),
	*function_json_implode	(char *),
	*function_jsontest	(char *),
	*function_key 		(char *),
	*function_killpid	(char *),
	*function_leftpc	(char *),
	*function_leftw 	(char *),
	*function_levelctl	(char *),
	*function_levelwindow	(char *),
	*function_loadinfo	(char *),
	*function_log		(char *),
	*function_log10		(char *),
	*function_logctl	(char *),
	*function_longtoip	(char *),
	*function_mask		(char *),
	*function_maxlen	(char *),
	*function_metric_time	(char *),
	*function_midw 		(char *),
	*function_mkdir		(char *),
	*function_mktime	(char *),
	*function_msar		(char *),
	*function_nametoip 	(char *),
	*function_nochops 	(char *),
	*function_nohighlight	(char *),
	*function_notify	(char *),
	*function_notifywindows	(char *),
	*function_notw 		(char *),
	*function_numlines	(char *),
	*function_numonchannel 	(char *),
	*function_numsort	(char *),
	*function_numwords 	(char *),
	*function_onchannel 	(char *),
	*function_open 		(char *),
	*function_outputinfo	(char *),
	*function_pad		(char *),
	*function_pattern 	(char *),
	*function_pass		(char *),
#ifdef HAVE_PERL
	*function_perl		(char *),
	*function_perlcall	(char *),
	*function_perlxcall	(char *),
#endif
#ifdef HAVE_PYTHON
	*function_pydirect	(char *),
	*function_python	(char *),
#endif
	*function_prefix	(char *),
	*function_printlen	(char *),
	*function_querywin	(char *),
	*function_qword		(char *),
	*function_randread	(char *),
	*function_read 		(char *),
	*function_realpath	(char *),
	*function_regcomp	(char *),
	*function_regcomp_cs	(char *),
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
	*function_rgb		(char *),
	*function_rfilter 	(char *),
	*function_rightw 	(char *),
	*function_rmdir 	(char *),
	*function_rpattern 	(char *),
	*function_rsubstr	(char *),
#ifdef HAVE_RUBY
	*function_ruby		(char *),
#endif
	*function_sar 		(char *),
	*function_seek		(char *),
	*function_server_version (char *),
	*function_serverctl	(char *),
	*function_servports	(char *),
	*function_serverwin	(char *),
	*function_sin		(char *),
	*function_sinh		(char *),
	*function_skip		(char *),
	*function_sort		(char *),
	*function_split 	(char *),
	*function_splitw	(char *),
	*function_splice 	(char *),
	*function_ssl		(char *),
	*function_startupfile	(char *),
	*function_stat		(char *),
	*function_status	(char *),
	*function_stripansi	(char *),
	*function_stripansicodes(char *),
	*function_stripc 	(char *),
	*function_stripcrap	(char *),
	*function_strlen	(char *),
	*function_strptime	(char *),
	*function_strtol	(char *),
	*function_substr	(char *),
	*function_symbolctl	(char *),
	*function_tan		(char *),
	*function_tanh		(char *),
	*function_tell		(char *),
	*function_timerctl	(char *),
	*function_tobase	(char *),
        *function_tow		(char *),
	*function_translate 	(char *),
	*function_truncate 	(char *),
	*function_ttyname	(char *),
	*function_twiddle	(char *),
	*function_uhc		(char *),
	*function_umask		(char *),
	*function_umode		(char *),
	*function_uname		(char *),
	*function_unicode	(char *),
	*function_uniq		(char *),
	*function_unlink 	(char *),
	*function_unsplit	(char *),
	*function_uuid4		(char *),
	*function_which 	(char *),
	*function_winchan	(char *),
	*function_windowctl	(char *),
	*function_wordtoindex	(char *),
	*function_write 	(char *),
	*function_writeb	(char *),
	*function_xform		(char *),
	*function_yn		(char *),
	*function_cp437test	(char *);

char	*wrapper_pattern	(char *, int),
	*wrapper_rpattern	(char *, int);

extern char
	*function_cparse	(char *),
	*function_push		(char *),
	*function_pop		(char *),
	*function_shift		(char *),
        *function_unshift	(char *),
	*function_shiftbrace	(char *);

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
	{ "ASCII",		function_ascii 		},
	{ "ASCIIQ",		function_asciiq 	},
	{ "ASIN",		function_asin		},
	{ "ASINH",		function_asinh		},
	{ "ATAN",		function_atan		},
	{ "ATANH",		function_atanh		},
	{ "BEFORE",             function_before 	},
	{ "BEFOREW",            function_beforew 	},
	{ "BINDCTL",		function_bindctl	},
	{ "BUILTIN_EXPANDO",	function_builtin	},
	{ "CEIL",		function_ceil	 	},
	{ "CENTER",		function_center 	},
	{ "CEXIST",		function_cexist		},
	{ "CHANKEY",		function_chankey	},
	{ "CHANLIMIT",		function_channellimit	},
	{ "CHANMODE",		function_channelmode	},
	{ "CHANNEL",		function_channel	},
	{ "CHANUSERS",		function_onchannel 	},
	{ "CHANWIN",		function_winchan	},
	{ "CHANSYNCING",	function_channelsyncing },
	{ "CHECK_CODE",		function_check_code	},
	{ "CHMOD",		function_chmod		},
	{ "CHNGW",              function_chngw 		},
	{ "CHOP",		function_chop		},
	{ "CHOPS",              function_chops 		},
	{ "CHR",		function_chr 		},
	{ "CHRQ",		function_chrq 		},
	{ "CIPHER",		function_cipher		},
	{ "CLOSE",		function_close 		},
	{ "COFILTER",		function_cofilter	},
	{ "COMMON",             function_common 	},
	{ "CONNECT",		function_connect 	},
	{ "CONVERT",		function_convert 	},
	{ "COPATTERN",          function_copattern 	},
	{ "CORFILTER",		function_corfilter	},
	{ "CORPATTERN",		function_corpattern 	},
	{ "COS",		function_cos		},
	{ "COSH",		function_cosh		},
	{ "COUNT",		function_count		},
	{ "CP437TEST",		function_cp437test	},
	{ "CPARSE",		function_cparse		},
	{ "CRYPT",		function_crypt		},
	{ "CTCPCTL",		function_ctcpctl	},
	{ "CURCMD",		function_curcmd		},
	{ "CURPOS",		function_curpos 	},
	{ "CURRCHANS",		function_currchans	},
	{ "DBMCTL",		function_dbmctl		},
	{ "DCCCTL",		function_dccctl		},
	{ "DELARRAY",           function_delarray 	},
	{ "DELITEM",            function_delitem	},
	{ "DELITEMS",           function_delitems	},
	{ "DEUHC",		function_deuhc		},
	{ "DIFF",               function_diff 		},
	{ "ENCODINGCTL",	function_encodingctl	},
	{ "ENCRYPTPARM",	function_encryptparm	},
	{ "EOF",		function_eof 		},
	{ "EPIC",		function_epic		},
	{ "EXEC",		function_exec		},
	{ "EXECCTL",		function_execctl	},
	{ "EXP",		function_exp		},
	{ "FERROR",		function_error		},
	{ "FEXIST",             function_fexist 	},
	{ "FILTER",             function_filter 	},
	{ "FINDITEM",		function_finditem 	},
	{ "FINDITEMS",		function_finditems 	},
	{ "FINDW",		function_findw		},
	{ "FINDWS",		function_findws		},
	{ "FIX_ARGLIST",	function_fix_arglist	},
        { "FIX_WIDTH",          function_fix_width      },
	{ "FLOODINFO",		function_floodinfo	},
	{ "FLOOR",		function_floor		},
	{ "FNEXIST",		function_fnexist	},
	{ "FREWIND",		function_rewind		},
	{ "FROMW",              function_fromw 		},
	{ "FSEEK",		function_seek		},
	{ "FSIZE",		function_fsize		},
	{ "FSKIP",		function_skip		},
	{ "FTELL",		function_tell		},
	{ "FTIME",		function_ftime		},
	{ "FTRUNCATE",		function_ftruncate 	},
	{ "FUNCTIONCALL",	function_functioncall	},
	{ "GEOM",		function_geom		},
	{ "GETARRAYS",          function_getarrays 	},
	{ "GETCAP",		function_getcap		},
	{ "GETCOMMANDS",	function_getcommands	},
	{ "GETENV",		function_getenv		},
	{ "GETFUNCTIONS",	function_getfunctions	},
	{ "GETGID",		function_getgid		},
	{ "GETITEM",            function_getitem 	},
	{ "GETLOGIN",		function_getlogin	},
	{ "GETMATCHES",         function_getmatches 	},
	{ "GETOPT",		function_getopt		},
	{ "GETPGRP",		function_getpgrp	},
	{ "GETRMATCHES",        function_getrmatches 	},
	{ "GETSERIAL",		function_getserial	},
	{ "GETSET",		function_getset		},
	{ "GETSETS",		function_getsets	},
	{ "GETTMATCH",		function_gettmatch	},
	{ "GETUID",		function_getuid		},
	{ "GLOB",		function_glob		},
	{ "GLOBI",		function_globi		},
	{ "HASH_32BIT",		function_hash_32bit	},
#if 0
	{ "HELP_TOPICS",	function_help_topics	},
#endif
	{ "HOOKCTL",		function_hookctl	},
	{ "ICONVCTL",		function_iconvctl	},
	{ "IDLE",		function_idle		},
	{ "IFINDFIRST",		function_ifindfirst 	},
	{ "IFINDITEM",		function_ifinditem	},
	{ "IFINDITEMS",		function_ifinditems	},
	{ "IGETITEM",           function_igetitem 	},
	{ "IGETMATCHES",	function_igetmatches	},
	{ "IGETRMATCHES",	function_igetrmatches	},
	{ "IGNORECTL",		function_ignorectl	},
	{ "INDEX",		function_index 		},
	{ "INDEXTOITEM",        function_indextoitem 	},
	{ "INDEXTOWORD",	function_indextoword	},
	{ "INFO",		function_info		},
	{ "INPUTCTL",		function_inputctl	},
	{ "INSERT",		function_insert 	},
	{ "INSERTW",            function_insertw 	},
	{ "IPTOLONG",		function_iptolong	},
	{ "IPTONAME",		function_iptoname 	},
	{ "IRCLIB",		function_irclib		},
	{ "IS8BIT",		function_is8bit 	},
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
	{ "ISUTF8",		function_isutf8		},
	{ "ITEMTOINDEX",        function_itemtoindex 	},
	{ "JN",			function_jn		},
	{ "JOINSTR",		function_joinstr	},
	{ "JOT",                function_jot 		},
	{ "JSON_ERROR",         function_json_error 	},
	{ "JSON_EXPLODE",       function_json_explode 	},
	{ "JSON_IMPLODE",       function_json_implode 	},
	{ "JSONTEST",       	function_jsontest 	},
	{ "KEY",                function_key 		},
	{ "KILLPID",		function_killpid	},
	{ "LASTLOG",		function_lastlog	}, /* lastlog.h */
	{ "LEFT",		function_left 		},
	{ "LEFTPC",		function_leftpc		},
	{ "LEFTW",              function_leftw 		},
	{ "LEVELCTL",		function_levelctl	},
	{ "LEVELWINDOW",	function_levelwindow	},
	{ "LINE",		function_line		}, /* lastlog.h */
	{ "LISTARRAY",		function_listarray	},
	{ "LISTEN",		function_listen 	},
	{ "LOADINFO",		function_loadinfo	},
	{ "LOG",		function_log		},
	{ "LOG10",		function_log10		},
	{ "LOGCTL",		function_logctl		}, /* logfiles.h */
	{ "LONGTOIP",		function_longtoip	},
	{ "MASK",		function_mask		},
	{ "MATCH",		function_match 		},
	{ "MATCHITEM",          function_matchitem 	},
	{ "MAXLEN",		function_maxlen		},
	{ "METRIC_TIME",	function_metric_time	},
	{ "MID",		function_mid 		},
	{ "MIDW",               function_midw 		},
	{ "MKDIR",		function_mkdir		},
	{ "MKTIME",		function_mktime		},
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
	{ "NUMLINES",		function_numlines	},
	{ "NUMONCHANNEL",	function_numonchannel 	},
	{ "NUMSORT",		function_numsort	},
	{ "NUMWORDS",		function_numwords	},
	{ "ONCHANNEL",          function_onchannel 	},
	{ "OPEN",		function_open 		},
	{ "OUTPUTINFO",		function_outputinfo	},
	{ "PAD",		function_pad		},
	{ "PASS",		function_pass		},
	{ "PATTERN",		function_pattern	},
#ifdef HAVE_PERL
	{ "PERL",		function_perl		},
	{ "PERLCALL",		function_perlcall	},
	{ "PERLXCALL",		function_perlxcall	},
#endif
#ifdef HAVE_PYTHON
	{ "PYDIRECT",		function_pydirect	},
	{ "PYTHON",		function_python		},
#endif
	{ "PID",		function_pid 		},
	{ "POP",		function_pop 		},
	{ "PPID",		function_ppid 		},
	{ "PREFIX",		function_prefix		},
	{ "PRINTLEN",		function_printlen	},
	{ "PUSH",		function_push 		},
	{ "QUERYWIN",		function_querywin	},
	{ "QWORD",		function_qword		},
	{ "RAND",		function_rand 		},
	{ "RANDREAD",		function_randread	},
	{ "READ",		function_read 		},
	{ "REALPATH",		function_realpath	},
	{ "REGCOMP",		function_regcomp	},
	{ "REGCOMP_CS",		function_regcomp_cs	},
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
	{ "RGB",		function_rgb		},
	{ "RIGHT",		function_right 		},
	{ "RIGHTW",             function_rightw 	},
	{ "RINDEX",		function_rindex 	},
	{ "RMATCH",		function_rmatch 	},
	{ "RMATCHITEM",         function_rmatchitem 	},
	{ "RMDIR",		function_rmdir 		},
	{ "RPATTERN",           function_rpattern 	},
	{ "RSUBSTR",		function_rsubstr	},
#ifdef HAVE_RUBY
	{ "RUBY",		function_ruby		},
#endif
	{ "SAR",		function_sar 		},
	{ "SERVERCTL",		function_serverctl	},
	{ "SERVERWIN",		function_serverwin	},
	{ "SERVPORTS",		function_servports	},
	{ "SETITEM",            function_setitem 	},
	{ "SHIFT",		function_shift 		},
	{ "SHIFTBRACE",		function_shiftbrace	},
	{ "SIN",		function_sin		},
	{ "SINH",		function_sinh		},
	{ "SORT",		function_sort		},
	{ "SPLICE",		function_splice 	},
	{ "SPLIT",		function_split 		},
	{ "SPLITW",		function_splitw 	},
	{ "SRAND",		function_srand 		},
	{ "SSL",		function_ssl		},
	{ "STARTUPFILE",	function_startupfile	},
	{ "STAT",		function_stat		},
	{ "STATUS",		function_status		},
	{ "STATUS_ONEOFF",	function_status_oneoff	},
	{ "STIME",		function_stime 		},
	{ "STRFTIME",		function_strftime	},
	{ "STRIP",		function_strip 		},
	{ "STRIPANSI",		function_stripansi	},
	{ "STRIPANSICODES",	function_stripansicodes },
	{ "STRIPC",		function_stripc		},
	{ "STRIPCRAP",		function_stripcrap	},
	{ "STRLEN",		function_strlen		},
	{ "STRPTIME",		function_strptime	},
	{ "STRTOL",		function_strtol		},
	{ "SUBSTR",		function_substr		},
	{ "SYMBOLCTL",		function_symbolctl	},
	{ "TAN",		function_tan		},
	{ "TANH",		function_tanh		},
	{ "TDIFF",		function_tdiff 		},
	{ "TDIFF2",		function_tdiff2 	},
	{ "TIME",		function_time 		},
	{ "TIMERCTL",		function_timerctl	},
	{ "TOBASE",		function_tobase 	},
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
	{ "UNICODE",		function_unicode 	},
	{ "UNIQ",		function_uniq		},
	{ "UNLINK",		function_unlink 	},
	{ "UNSHIFT",		function_unshift 	},
	{ "UNSPLIT",		function_unsplit	},
	{ "USERHOST",		function_userhost 	},
	{ "USERMODE",		function_umode		},
	{ "USETITEM",           function_usetitem 	},
	{ "UTIME",		function_utime	 	},
	{ "UUID4",		function_uuid4		},
	{ "VERSION",		function_server_version },
	{ "WHICH",		function_which 		},
	{ "WINCHAN",		function_winchan	},
	{ "WINDOWCTL",		function_windowctl	},
	{ "WORD",		function_word 		},
	{ "WORDTOINDEX",	function_wordtoindex	},
	{ "WRITE",		function_write 		},
	{ "WRITEB",		function_writeb		},
	{ "XDEBUG",		function_xdebug		},
	{ "XFORM",		function_xform		},
	{ "YN",			function_yn		},
	{ (char *) 0,		NULL }
};

void	init_expandos (void)
{
	int	i;

	for (i = 0; built_in[i].name; i++)
		add_builtin_expando(built_in[i].name, built_in[i].func);
}

void	init_functions (void)
{
	int	i;

	for (i = 0; built_in_functions[i].name; i++)
		add_builtin_func_alias(built_in_functions[i].name, built_in_functions[i].func);
}


/*
 * call_function has changed a little bit.  Now we take the entire call
 * including args in the paren list.  This is a bit more convenient for 
 * the callers, since there are a bunch of them and all of them seperately
 * handling extracting the args is just a pain in the butt.
 */
char	*call_function (char *name, const char *args)
{
	char	*tmp;
	char	*result = (char *) 0;
	char	*debug_copy = (char *) 0;
	char	*lparen;
	int	debugging;
	size_t	size;
	char *	buf;
	const char *	alias;
	char *	(*func) (char *) = NULL;
	void *	arglist = NULL;
	size_t	type;
	char *	str = NULL;

	debugging = get_int_var(DEBUG_VAR);

	if ((lparen = strchr(name, '(')))
	{
		ssize_t	span;

		if ((span = MatchingBracket(lparen + 1, '(', ')')) >= 0)
			lparen[1 + span] = 0;
		else
			yell("Unmatched lparen in function call [%s]", name);

		*lparen++ = 0;
	}
	else
		lparen = endstr(name);

	upper(name);
	type = strspn(name, ":");
	name += type;

	str = remove_brackets(name, args);
	alias = get_func_alias(str, &arglist, &func);

	if ((type == 0 && (!func && !alias)) ||
	    (type == 1 && !alias) ||
	    (type == 2 && !func))
	{
	    if (x_debug & DEBUG_UNKNOWN)
		yell("Function call to non-existant alias [%s]", str);
	    if (debugging & DEBUG_FUNCTIONS)
		privileged_yell("Function %s(%s) returned ", str, lparen);
	    new_free(&str);
            return malloc_strdup(empty_string);
        }

	tmp = expand_alias(lparen, args);
	debug_copy = LOCAL_COPY(tmp);

	if (func && type != 1)
		result = func(tmp);
	else if (alias && type != 2)
		result = call_user_function(str, alias, tmp, arglist);

	size = strlen(str) + strlen(debug_copy) + 15;
	buf = (char *)alloca(size);
	snprintf(buf, size, "$%s(%s)", str, debug_copy);
	MUST_BE_MALLOCED(result, buf);

	if (debugging & DEBUG_FUNCTIONS)
		privileged_yell("Function %s(%s) returned %s", 
					str, debug_copy, result);

	new_free(&str);
	new_free(&tmp);
	return result;
}

static int	func_exist (char *command)
{
	char *	name;
	char *	(*func) (char *);

	if (!command || !*command)
		return 0;

	name = LOCAL_COPY(command);
	upper(name);

	get_func_alias(name, NULL, &func);
	if (func == NULL)
		return 0;
	return 1;
}

/*
 * Convert a json object string into arguments as described by 'kwargs'.
 */
int	parse_kwargs (struct kwargs *kwargs, const char *input)
{
	cJSON *json = cJSON_Parse(input);
	int	i;

	if (json == NULL)
	{
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL)
			yell("Error before: %s\n", error_ptr);
		return -1;
	}

	while (kwargs && kwargs->kwarg)
	{
		const cJSON *value;

		value = cJSON_GetObjectItem(json, kwargs->kwarg);
		if (kwargs->type == KWARG_TYPE_STRING)
		{
			if (cJSON_IsInvalid(value))
			{
				malloc_strcpy((char **)kwargs->data, empty_string);
			}
			else if (cJSON_IsFalse(value))
			{
				malloc_strcpy((char **)kwargs->data, zero);
			}
			else if (cJSON_IsTrue(value))
			{
				malloc_strcpy((char **)kwargs->data, one);
			}
			else if (cJSON_IsNull(value))
			{
				malloc_strcpy((char **)kwargs->data, empty_string);
			}
			else if (cJSON_IsNumber(value))
			{
				malloc_sprintf((char **)kwargs->data, "%f", value->valuedouble);
			}
			else if (cJSON_IsString(value) && (value->valuestring != NULL))
			{
				malloc_strcpy((char **)kwargs->data, value->valuestring);
			}
			else if (cJSON_IsArray(value))
			{
				malloc_strcpy((char **)kwargs->data, "<unsupported array>");
			}
			else if (cJSON_IsObject(value))
			{
				malloc_strcpy((char **)kwargs->data, "<unsupported dict>");
			}
			else if (kwargs->required)
				say("kwarg %s not found or was invalid", kwargs->kwarg);
		}
		if (kwargs->type == KWARG_TYPE_INTEGER)
		{
			if (cJSON_IsInvalid(value))
			{
				*(intmax_t *)kwargs->data = 0;
			}
			else if (cJSON_IsFalse(value))
			{
				*(intmax_t *)kwargs->data = 0;
			}
			else if (cJSON_IsTrue(value))
			{
				*(intmax_t *)kwargs->data = 1;
			}
			else if (cJSON_IsNull(value))
			{
				*(intmax_t *)kwargs->data = 0;
			}
			else if (cJSON_IsNumber(value))
			{
				*(intmax_t *)kwargs->data = (intmax_t)value->valuedouble;
			}
			else if (cJSON_IsString(value) && (value->valuestring != NULL))
			{
				char *end = NULL;
				*(intmax_t *)kwargs->data = (intmax_t)strtoimax(value->valuestring, &end, 0);
			}
			else if (cJSON_IsArray(value))
			{
				*(intmax_t *)kwargs->data = 0;
			}
			else if (cJSON_IsObject(value))
			{
				*(intmax_t *)kwargs->data = 0;
			}
			else if (kwargs->required)
				say("kwarg %s not found or was invalid", kwargs->kwarg);
		}
		if (kwargs->type == KWARG_TYPE_NUMBER)
		{
			if (cJSON_IsInvalid(value))
			{
				*(long double *)kwargs->data = 0;
			}
			else if (cJSON_IsFalse(value))
			{
				*(long double *)kwargs->data = 0;
			}
			else if (cJSON_IsTrue(value))
			{
				*(long double *)kwargs->data = 1;
			}
			else if (cJSON_IsNull(value))
			{
				*(long double *)kwargs->data = 0;
			}
			else if (cJSON_IsNumber(value))
			{
				*(long double *)kwargs->data = (long double)value->valuedouble;
			}
			else if (cJSON_IsString(value) && (value->valuestring != NULL))
			{
				char *end = NULL;
				*(long double *)kwargs->data = (long double)strtod(value->valuestring, &end);
			}
			else if (cJSON_IsArray(value))
			{
				*(long double *)kwargs->data = 0;
			}
			else if (cJSON_IsObject(value))
			{
				*(long double *)kwargs->data = 0;
			}
			else if (kwargs->required)
				say("kwarg %s not found or was invalid", kwargs->kwarg);
		}
		if (kwargs->type == KWARG_TYPE_BOOL)
		{
			if (cJSON_IsInvalid(value))
			{
				*(int *)kwargs->data = 0;
			}
			else if (cJSON_IsFalse(value))
			{
				*(int *)kwargs->data = 0;
			}
			else if (cJSON_IsTrue(value))
			{
				*(int *)kwargs->data = 1;
			}
			else if (cJSON_IsNull(value))
			{
				*(int *)kwargs->data = 0;
			}
			else if (cJSON_IsNumber(value))
			{
				if (value->valuedouble)
					*(int *)kwargs->data = 1;
				else
					*(int *)kwargs->data = 0;
			}
			else if (cJSON_IsString(value))
			{
				if (!value->valuestring || !*value->valuestring)
					*(int *)kwargs->data = 0;
				else if (!my_stricmp(value->valuestring, zero))
					*(int *)kwargs->data = 0;
				else
					*(int *)kwargs->data = 1;
			}
			else if (cJSON_IsArray(value))
			{
				*(int *)kwargs->data = 0;
			}
			else if (cJSON_IsObject(value))
			{
				*(int *)kwargs->data = 0;
			}
			else if (kwargs->required)
				say("kwarg %s not found or was invalid", kwargs->kwarg);
		}

		kwargs++;
	}
	cJSON_Delete(json);
	return 0;
}


/* built in expando functions */
static	char	*alias_line 		(void) { return malloc_strdup(get_input()); }
static	char	*alias_buffer 		(void) { return malloc_strdup(cut_buffer); }
static	char	*alias_time 		(void) { return malloc_strdup(get_clock()); }
static	char	*alias_dollar 		(void) { return malloc_strdup("$"); }
static	char	*alias_detected 	(void) { return malloc_strdup(last_notify_nick); }
static	char	*alias_nick 		(void) { return malloc_strdup((get_window_server(0) != NOSERV) ? get_server_nickname(get_window_server(0)) : empty_string); }
static	char	*alias_away 		(void) { return malloc_strdup(get_server_away(from_server)); }
static  char    *alias_sent_nick        (void) { return malloc_strdup((get_server_sent_nick(from_server)) ? get_server_sent_nick(from_server) : empty_string); }
static  char    *alias_recv_nick        (void) { return malloc_strdup((get_server_recv_nick(from_server)) ? get_server_recv_nick(from_server) : empty_string); }
static  char    *alias_msg_body         (void) { return malloc_strdup((get_server_sent_body(from_server)) ? get_server_sent_body(from_server) : empty_string); }
static  char    *alias_joined_nick      (void) { return malloc_strdup((get_server_joined_nick(from_server)) ? get_server_joined_nick(from_server) : empty_string); }
static  char    *alias_public_nick      (void) { return malloc_strdup((get_server_public_nick(from_server)) ? get_server_public_nick(from_server) : empty_string); }
static  char    *alias_show_realname 	(void) { 
	return malloc_strdup(
		(get_window_server(0) != NOSERV) ? 
		get_server_realname(get_window_server(0)) : 
		empty_string); }
static	char	*alias_version_str 	(void) { return malloc_strdup(irc_version); }
static  char    *alias_invite           (void) { return malloc_strdup((get_server_invite_channel(from_server)) ? get_server_invite_channel(from_server) : empty_string); }
static	char	*alias_oper 		(void) { return malloc_strdup((from_server != -1) ? get_server_operator(from_server) ?  get_string_var(STATUS_OPER_VAR) : empty_string : empty_string); }
static	char	*alias_version 		(void) { return malloc_strdup(internal_version); }
static  char    *alias_show_userhost 	(void) { return malloc_strdup(get_server_userhost(from_server)); }
static  char    *alias_online 		(void) { return malloc_sprintf(NULL, INTMAX_FORMAT, (intmax_t)start_time.tv_sec); }
static  char    *alias_idle 		(void) { return malloc_sprintf(NULL, INTMAX_FORMAT, (intmax_t)time(NULL) - idle_time.tv_sec); }
static	char	*alias_current_numeric	(void) { return malloc_sprintf(NULL, "%03d", current_numeric); }
static	char	*alias_banner		(void) { return malloc_strdup(banner()); }

static	char	*alias_currdir  	(void)
{
	char 	*tmp = (char *)new_malloc(PATH_MAX+1);
	if (!getcwd(tmp, PATH_MAX))
		*tmp = 0;
	return tmp;
}

static	char	*alias_channel 		(void) 
{ 
	const char	*tmp; 
	return malloc_strdup((tmp = get_window_echannel(0)) ? tmp : zero);
}

static	char	*alias_server 		(void)
{
	return malloc_strdup((parsing_server_index != NOSERV) ?
		         get_server_itsname(parsing_server_index) :
		         (get_window_server(0) != NOSERV) ?
			        get_server_itsname(get_window_server(0)) : 
				empty_string);
}

static	char	*alias_query_nick 	(void)
{
	const char	*tmp;
	return malloc_strdup((tmp = get_window_equery(0)) ? tmp : empty_string);
}

static	char	*alias_target 		(void)
{
	const char	*tmp;
	return malloc_strdup((tmp = get_window_target(0)) ? tmp : empty_string);
}

static	char	*alias_cmdchar 		(void)
{
	const char *cmdchars;
	char tmp[2];

	if ((cmdchars = get_string_var(CMDCHARS_VAR)) == (char *) 0)
		cmdchars = DEFAULT_CMDCHARS;
	tmp[0] = cmdchars[0];
	tmp[1] = 0;
	return malloc_strdup(tmp);
}

static	char	*alias_chanop 		(void)
{
	const char	*tmp;
	return malloc_strdup(((tmp = get_window_echannel(0)) && get_channel_oper(tmp, get_window_server(0))) ?
		"@" : empty_string);
}

static	char	*alias_modes 		(void)
{
	const char	*tmp;
	return malloc_strdup((tmp = get_window_echannel(0)) ?
		get_channel_mode(tmp, get_window_server(0)) : empty_string);
}

static	char	*alias_server_version  (void)
{
	int s = from_server;

	if (s == NOSERV)
	{
		if (primary_server != NOSERV)
			s = primary_server;
		else
			return malloc_strdup(empty_string);
	}

	return malloc_strdup(get_server_version_string(s));
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
 * Usage: $left(number text)
 * Returns: the <number> leftmost code points in <text>.
 * Example: $left(5 the quick brown frog) returns "the q"
 *
 * Note: the difference between $[10]foo and $left(10 foo) is that the former
 * is padded and the latter is not.
 *
 * Note: This function counts code points, not columns!  So non-printable
 * codepoints (like ^B/^V/^C) still count!
 */
BUILT_IN_FUNCTION(function_left, word)
{
	int		keepers,	/* The number of CPs to retain */
			count,		/* How many we've copied so far */
			code_point;	/* The current CP we're working on */
	unsigned char 	*s;		/* Pointer at next CP */

	GET_INT_ARG(keepers, word);
	RETURN_IF_EMPTY(word);

	if (keepers <= 0)
		RETURN_EMPTY;

	/* Return the whole string if it's "short" */
	if (keepers >= quick_code_point_count(word))
		RETURN_STR(word);

	count = 0;
	s = word;
	while ((code_point = next_code_point(CUC_PP &s, 0)))
	{
		/* Invalid CPs count as 1, + we skip them. */
		if (code_point == -1)
			s++;

		if (++count >= keepers)
			break;
	}

	/* Chop the string off here */
	*s = 0;
	RETURN_STR(word);
}

/*
 * Usage: $right(number text)
 * Returns: the <number> rightmost characters in <text>.
 * Example: $right(5 the quick brown frog) returns " frog"
 * XXX This function should use previous_code_point() like $rest() does.
 */
BUILT_IN_FUNCTION(function_right, word)
{
	int	keepers,	/* The number of CPs to retain */
		count,		/* How many we've copied so far */
		code_point,	/* The current CP we're working on */
		total,		/* How many CPs are in word */
		ignores;	/* Leading CPs to ignore */
	char 	*s;		/* Pointer at next CP */

	GET_INT_ARG(keepers, word);
	RETURN_IF_EMPTY(word);

	if (keepers <= 0)
		RETURN_EMPTY;

	/* Return the whole string if it's "short" */
	if (keepers >= ((total = quick_code_point_count(word))))
		RETURN_STR(word);

	/* Skip the first 'ignores' CPs */
	ignores = total - keepers;
	s = word;
	while ((code_point = next_code_point(CUC_PP &s, 0)))
	{
		/* Invalid CPs count as 1, + we skip them. */
		if (code_point == -1)
			s++;

		if (--ignores <= 0)
			break;
	}

	RETURN_STR(s);
}

/*
 * Usage: $mid(start number text)
 * Returns: the <start>th through <start>+<number>th characters in <text>.
 * Example: $mid(3 4 the quick brown frog) returns " qui"
 *
 * Note: the first character is numbered zero.
 * XXX It's a shame this isn't generalized and shared with other funcs.
 */
BUILT_IN_FUNCTION(function_mid, word)
{
	int	keepers,	/* The number of CPs to retain */
		count,		/* How many we've copied so far */
		code_point,	/* The current CP we're working on */
		total;		/* How many CPs are in word */
	int	start;
	char 	*s;		/* Pointer at next CP */
	char	*retval;

	GET_INT_ARG(start, word);
	GET_INT_ARG(keepers, word);
	RETURN_IF_EMPTY(word);

	if (keepers <= 0)
		RETURN_EMPTY;

	if (start < 0)
		RETURN_EMPTY;
	if (start > quick_code_point_count(word))
		RETURN_EMPTY;

	/* Skip the initial CPs */
	for (s = word, count = 0; count < start; count++)
	{
		/* Invalid CPs count as 1, and we skip them. */
		code_point = next_code_point(CUC_PP &s, 0);
		if (code_point == -1)
			s++;
	}

	/* This is our anchor */
	retval = s;

	/* Return the whole string if it's "short" */
	if (keepers >= quick_code_point_count(retval))
		RETURN_STR(retval);

	/* Otherwise count off 'keepers' CPs */
	count = 0;
	s = retval;
	while ((code_point = next_code_point(CUC_PP &s, 0)))
	{
		/* Invalid CPs count as 1, + we skip them. */
		if (code_point == -1)
			s++;

		if (++count >= keepers)
			break;
	}

	/* Chop the string off here */
	*s = 0;
	RETURN_STR(retval);
}


/*
 * Usage: $rand(max)
 * Returns: A random number from zero to max-1.
 * Example: $rand(10) might return any number from 0 to 9.
 */
BUILT_IN_FUNCTION(function_rand, word)
{
	unsigned long	tempin, ret;
static	unsigned long	rn = 0;

	GET_INT_ARG(tempin, word);
	if (tempin == 0)
		ret = random_number(0);
	else {
		if (rn < tempin)
			rn ^= random_number(0);
		ret = rn % tempin;
		rn /= tempin;
	}
	RETURN_INT(ret);
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
	const char	*ret;

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

	/* XXX Why doesn't this use GET_INT_ARG? */
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
			strlcat(tmp, "1 day ", size);
		else
			strlpcat(tmp, size, "%ld days ", (long)days);
	}
	if (hours)
	{
		if (hours == 1)
			strlcat(tmp, "1 hour ", size);
		else
			strlpcat(tmp, size, "%ld hours ", (long)hours);
	}
	if (minutes)
	{
		if (minutes == 1)
			strlcat(tmp, "1 minute ", size);
		else
			strlpcat(tmp, size, "%ld minutes ", (long)minutes);
	}

	if (seconds || (!days && !hours && !minutes) || 
			(*after == '.' && is_number(after + 1)))
	{
		unsigned long number = 0;

		/*
		 * If we have a decmial point, and is_number() returns 1,
		 * then we know that we have a real, authentic number AFTER
		 * the decmial point.  As long as it isnt zero, we want it.
		 */
		strlcat(tmp, NUMSTR(seconds), size);
		if (*after == '.')
		{
			if ((number = atol(after + 1)))
				strlcat(tmp, after, size);
		}

		if (seconds == 1 && number == 0)
			strlcat(tmp, " second", size);
		else
			strlcat(tmp, " seconds", size);
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
	char *schars;
	size_t	cpoffset;

	GET_DWORD_ARG(schars, input);
	cpoffset = -1;
	cpindex((const unsigned char *)input, (const unsigned char *)schars, 1, &cpoffset);
	RETURN_INT(cpoffset);
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
	char	*chars;
	size_t	cpoffset;

	/* need to find out why ^x doesnt work */
	GET_DWORD_ARG(chars, word);
	cpoffset = -1;
	if (!*word || !*chars)
		RETURN_INT(-1);

	rcpindex((const unsigned char *)word + strlen(word), 
		 (const unsigned char *)word, 
		 (const unsigned char *)chars, 1, &cpoffset);
	RETURN_INT(cpoffset);
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

	GET_FUNC_ARG(pattern, input);

	while ((word = next_func_arg(input, &input)))
	{
		match_index++;
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

	GET_FUNC_ARG(word, input);

	while ((pattern = next_func_arg(input, &input)))
	{
		rmatch_index++;
		if ((current_match = wild_match(pattern, word)) > best_match)
		{
			match = rmatch_index;
			best_match = current_match;
		}
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
		const char *uh;
		const char *chan = NULL;

		while (input && *input)
		{
			GET_FUNC_ARG(nick, input);
			if (is_channel(nick))
				chan = nick;
			if ((uh = fetch_userhost(from_server, chan, nick)))
				malloc_strcat_word_c(&retval, space, uh, DWORD_NO, &rvclue);
			else
				malloc_strcat_word_c(&retval, space, unknown_userhost, DWORD_NO, &rvclue);
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
 * To remove spaces, use $strip(" " text)
 */
BUILT_IN_FUNCTION(function_strip, input)
{
	char *	search;
	const unsigned char 	*s, *p;
	int	c, d;
	int	found;
	char *	result, *r;

	GET_DWORD_ARG(search, input);
	RETURN_IF_EMPTY(input);

	r = result = (char *)new_malloc(strlen(input) + 1);

	p = input;
	while ((c = next_code_point(&p, 1)))
	{
		found = 0;
		s = search;
		while ((d = next_code_point(&s, 1)))
		{
			if (c == d)
			{
				found = 1;
				break;
			}
		}
		if (!found)
		{
			unsigned char utf8str[16];
			unsigned char *x;

			ucs_to_utf8(c, utf8str, sizeof(utf8str));
			for (x = utf8str; *x; x++)
				*r++ = *x;
		}
	}
	*r = 0;
	return result;		/* DONT USE RETURN_STR HERE! */
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
	char *	channel;
	int	ret;

	channel = next_func_arg(input, &input);
	ret = is_channel(channel);
	RETURN_INT(ret);
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
	char 	*nick, *chan;
	int	ret;

	nick = next_func_arg(input, &input);
	chan = next_func_arg(input, &input);
	ret = is_chanop(chan, nick);
	RETURN_INT(ret);
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

	while (cvalue-- > 0 && word && *word)
		next_func_arg(word, &word);

	GET_FUNC_ARG(w_word, word);
	RETURN_STR(w_word);
}

/*
 * Usage: $qword(number text)
 * Returns: the <number>th word in <text>, but as a qword, suitable for 
 *          passing back into a function call as a dword.  
 *          The first word is numbered zero.
 * Example: $qword(3 one two three four five) returns "four" (think about it)
 * Remember, double quoted words require /xdebug dword to be turned on!
 */
BUILT_IN_FUNCTION(function_qword, word)
{
	int	cvalue;
	char *	w_word;
	char *	retval = NULL;

	GET_INT_ARG(cvalue, word);
	if (cvalue < 0)
		RETURN_EMPTY;

	while (cvalue-- > 0 && word && *word)
		next_func_arg(word, &word);

	GET_FUNC_ARG(w_word, word);
	malloc_strcat_word(&retval, space, w_word, DWORD_DWORDS);
	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_connect, input)
{
	char *	host;
	char *	port;
	char *	v;
	int	family = AF_INET;

	GET_FUNC_ARG(host, input);
	GET_FUNC_ARG(port, input);
	if (input && *input)
	{
		GET_FUNC_ARG(v, input)

		/* Figure out what family the user wants */
		if (*v == 'v' || *v == 'V')
			v++;
		if (*v == '4')
			family = AF_INET;
		else if (*v == '6')
			family = AF_INET6;
		else if (*v == 'u' || *v == 'U')
			family = AF_UNSPEC;
	}

	return dcc_raw_connect(host, port, family);	/* DONT USE RETURN_STR HERE! */
}

BUILT_IN_FUNCTION(function_listen, input)
{
	int	port = 0;
	char *	v;
	int	family = AF_INET;

	/* Oops. found by CrowMan, listen() has a default. erf. */
	if (input && *input)
	{
		char *tmp, *ptr;
		if ((tmp = next_func_arg(input, &input)))
		{
			port = strtoul(tmp, &ptr, 0);
			if (ptr == tmp)
				RETURN_EMPTY;	/* error. */
		}

		if (input && *input)
		{
			GET_FUNC_ARG(v, input)

			/* Figure out what family the user wants */
			if (*v == 'v' || *v == 'V')
				v++;
			if (*v == '4')
				family = AF_INET;
			else if (*v == '6')
				family = AF_INET6;
			else if (*v == 'u' || *v == 'U')
				family = AF_UNSPEC;
		}
	}

	return dcc_raw_listen(family, port);	/* DONT USE RETURN_STR HERE! */
}

BUILT_IN_FUNCTION(function_toupper, input)
{
	return (upper(malloc_strdup(input)));
}

BUILT_IN_FUNCTION(function_tolower, input)
{
	return (lower(malloc_strdup(input)));
}

BUILT_IN_FUNCTION(function_curpos, input)
{
	RETURN_INT(cursor_position(get_window_screen(0)));
}

BUILT_IN_FUNCTION(function_channels, input)
{
	int	server = from_server;
	char *	retval;

	if (isdigit(*input)) 
		GET_INT_ARG(server, input)
	else if (*input)
	{
		int	window;

		server = -1;

		/* 
		 * You may be wondering what I'm doing here.  It used to 
		 * be a historical idiom that you could do $mychannels(serv)
		 * or $mychannels(#window).  The "#" thing was handled else-
		 * where, but I took it out becuase it had evil side effects.
		 * But people need to be able to use "#" here, so specifically
		 * support "#" here if needed.
		 */
 		if ((window = lookup_window(input)) > 0)
			server = get_window_server(window);
		else if (*input == '#')
			if ((window = lookup_window(input + 1)) > 0)
				server = get_window_server(window);
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

	for (count = 0; count < server_list_size(); count++)
	{
		if (is_server_registered(count))
			malloc_strcat_word_c(&retval, space, ltoa(count), DWORD_NO, &rvclue);
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
		return malloc_strdup(empty_string);


	tm = localtime(&ltime);

	if (!strftime(result, 128, input, tm))
		return malloc_strdup(empty_string);

	return malloc_strdup(result);
}

BUILT_IN_FUNCTION(function_idle, input)
{
	return alias_idle();
}



/* The new "added" functions */

/* $before(chars string of text)
 * returns the part of "string of text" that occurs before the
 * first instance of any character in "chars"
 * EX:  $before(! nick!user@host.com) returns "nick"
 */
BUILT_IN_FUNCTION(function_before, word)
{
	char	*pointer = (char *) 0;
	char	*chars;
	char	*tmp;
	long	numint;

	GET_DWORD_ARG(tmp, word);			/* DONT DELETE TMP! */
	numint = my_atol(tmp);

	if (numint)
	{
		GET_DWORD_ARG(chars, word);
	}
	else
	{
		numint = 1;
		chars = tmp;
	}

	if (numint < 0 && strlen(word))
		pointer = word + strlen(word) - 1;

	pointer = search_for(word, &pointer, chars, numint);

	if (!pointer)
		RETURN_EMPTY;

	*pointer = '\0';
	RETURN_STR(word);
}

/* $after(chars string of text)
 * returns the part of "string of text" that occurs after the 
 * first instance of any character in "chars"
 * EX: $after(! nick!user@host.com)  returns "user@host.com"
 */
BUILT_IN_FUNCTION(function_after, word)
{
	char	*chars;
	char	*pointer = (char *) 0;
	char 	*tmp;
	long	numint;

	GET_DWORD_ARG(tmp, word);
	numint = my_atol(tmp);

	if (numint)
	{
		GET_DWORD_ARG(chars, word)
	}
	else
	{
		numint = 1;
		chars = tmp;
	}

	if (numint < 0 && strlen(word))
		pointer = word + strlen(word) - 1;

	pointer = search_for(word, &pointer, chars, numint);

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

	return (extractfw(word, 0, value-1));	/* DONT USE RETURN_STR HERE! */
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
		
	return extractfw2(word, -value, EOS); 
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

	return extractfw(word, start, (start + num - 1));
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
		part1 = extractfw(word, 0, (where - 1));
		part2 = extractfw(word, (where + 1), EOS);
		booya = malloc_strdup(part1);
		/* if part2 is there, append it. */
		malloc_strcat_wordlist(&booya, space, part2);
		new_free(&part1);
		new_free(&part2);
	}
	else /* where == 0 */
		booya = extractfw(word, 1, EOS);

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
	return extractfw(word, where, EOS);
}

/* 
 * $remw(word string of text)
 * returns "string of text" with the word "word" removed
 * EX: $remw(the now is the time for) returns "now is time for"
 * XXX Should use standard word manip functions 
 */
BUILT_IN_FUNCTION(function_remw, word)
{
	int	where;
	char *	lame = NULL;
	char *	placeholder;
	char *	booya;

	lame = LOCAL_COPY(word);
	where = my_atol((placeholder = function_findw(lame)));
	new_free(&placeholder);

	/* Whack off the word we're looking for... */
	GET_FUNC_ARG(lame, word);

	/* XXX Cut and pasted from $notw(). */
        /* An invalid word simply returns the string as-is */
        if (where < 0)
                RETURN_STR(word);

        if (where > 0)
        {
                char *part1, *part2;
                part1 = extractfw(word, 0, (where - 1));
                part2 = extractfw(word, (where + 1), EOS);
                booya = malloc_strdup(part1);
                /* if part2 is there, append it. */
                malloc_strcat_wordlist(&booya, space, part2);
                new_free(&part1);
                new_free(&part2);
        }
        else /* where == 0 */
                booya = extractfw(word, 1, EOS);

	return booya;			/* DON'T USE RETURN_STR HERE */
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
	size_t	clue = 0;

	GET_INT_ARG(where, word);
	
	/* If the word goes at the front of the string, then it
	   already is: return it. ;-) */
	if (where < 1)
		booya = malloc_strdup(word);
	else
	{
		GET_FUNC_ARG(what, word);
		str1 = extractfw(word, 0, (where - 1));
		str2 = extractfw(word, where, EOS);

		malloc_strcat_wordlist_c(&booya, space, str1, &clue);
		malloc_strcat_word_c(&booya, space, what, DWORD_DWORDS, &clue);
		malloc_strcat_wordlist_c(&booya, space, str2, &clue);

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
	char	*str1, *str2;
	
	GET_INT_ARG(which, word);
	GET_FUNC_ARG(what, word);

	if (which < 0)
		RETURN_STR(word);

	/* hmmm. if which is 0, extract does the wrong thing. */
	str1 = extractfw(word, 0, which - 1);
	str2 = extractfw(word, which + 1, EOS);

	malloc_strcat_word(&str1, space, what, DWORD_DWORDS);
	malloc_strcat_wordlist(&str1, space, str2);
	new_free(&str2);
	return str1;
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
	char    *left = (char *) 0;
	char	*right = (char *) 0;
	char 	*booya = NULL;
	char **leftw = NULL;
	char **rightw = NULL;
	int	leftc, lefti,
		rightc, righti;
	size_t	rvclue=0;

	left = word;
	if (!(right = strchr(word,'/')))
		RETURN_EMPTY;

	*right++ = 0;
	leftc = splitw(left, &leftw, DWORD_DWORDS);
	rightc = splitw(right, &rightw, DWORD_DWORDS);

	for (lefti = 0; lefti < leftc; lefti++)
	{
		for (righti = 0; righti < rightc; righti++)
		{
			if (rightw[righti] && !my_stricmp(leftw[lefti], rightw[righti]))
			{
				malloc_strcat_word_c(&booya, space, leftw[lefti], DWORD_DWORDS, &rvclue);
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
	char 	*left = NULL,
	     	*right = NULL, 
		*booya = NULL;
	char **rightw = NULL,
		   **leftw = NULL;
	int 	lefti, leftc,
	    	righti, rightc;
	int 	found;
	size_t	rvclue=0;

	left = word;
	if ((right = strchr(word, '/')) == (char *) 0)
		RETURN_EMPTY;

	*right++ = 0;
	leftc = splitw(left, &leftw, DWORD_DWORDS);
	rightc = splitw(right, &rightw, DWORD_DWORDS);

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
			malloc_strcat_word_c(&booya, space, leftw[lefti], DWORD_DWORDS, &rvclue);
	}

	for (rvclue = righti = 0; righti < rightc; righti++)
	{
		if (rightw[righti])
			malloc_strcat_word_c(&booya, space, rightw[righti], DWORD_DWORDS, &rvclue);
	}

	new_free((char **)&leftw);
	new_free((char **)&rightw);

	RETURN_MSTR(booya);
}

char *wrapper_pattern(char *word, int mode)
{
	char    *blah;
	char    *booya = NULL;
	char    *pattern;
	size_t	rvclue=0;

	GET_FUNC_ARG(pattern, word)
	while (((blah = next_func_arg(word, &word)) != NULL))
	{
		if (!!wild_match(pattern, blah) == !!mode)
			malloc_strcat_word_c(&booya, space, blah, DWORD_DWORDS, &rvclue);
	}
	RETURN_MSTR(booya);
}

/* $pattern(pattern string of words)
 * given a pattern and a string of words, returns all words that
 * are matched by the pattern
 * EX: $pattern(f* one two three four five) returns "four five"
 */

BUILT_IN_FUNCTION(function_pattern, word) {
	return(wrapper_pattern(word, 1));
}

/* $filter(pattern string of words)
 * given a pattern and a string of words, returns all words that are 
 * NOT matched by the pattern
 * $filter(f* one two three four five) returns "one two three"
 */

BUILT_IN_FUNCTION(function_filter, word) {
	return(wrapper_pattern(word, 0));
}

char *wrapper_rpattern (char *word, int mode)
{
	char    *blah;
	char    *booya = NULL;
	char    *pattern;
	size_t	rvclue=0;

	GET_FUNC_ARG(blah, word)

	while ((pattern = next_func_arg(word, &word)) != NULL)
	{
		if (!!wild_match(pattern, blah) == !!mode)
			malloc_strcat_word_c(&booya, space, pattern, DWORD_DWORDS, &rvclue);
	}
	RETURN_MSTR(booya);
}

/* $rpattern(word list of patterns)
 * Given a word and a list of patterns, return all patterns that
 * match the word.
 * EX: $rpattern(user@host.com *@* user@* f*@*.com)
 * returns "*@* user@*"
 */

BUILT_IN_FUNCTION(function_rpattern, word) {
	return(wrapper_rpattern(word, 1));
}

/* $rfilter(word list of patterns)
 * given a word and a list of patterns, return all patterns that
 * do NOT match the word
 * EX: $rfilter(user@host.com *@* user@* f*@*.com)
 * returns "f*@*.com"
 */

BUILT_IN_FUNCTION(function_rfilter, word) {
	return(wrapper_rpattern(word, 0));
}

/* $copattern(pattern var_1 var_2)
 * Given a pattern and two variable names, it returns all words
 * in the variable_2 corresponding to any words in variable_1 that
 * are matched by the pattern
 * EX: @nicks = [nick1 nick2 nick3]
 *     @userh = [user1@host1.com user2@host1.com user3@host2.com]
 *     $copattern(*@host1.com userh nicks) 
 *	returns "nick1 nick2"
 */
#define COPATFUNC(fn, pat, arg, sense)                                 \
BUILT_IN_FUNCTION((fn), word)                                          \
{                                                                      \
       char    *booya = (char *) 0,                                    \
	       *pattern = (char *) 0,                                  \
               *firstl = (char *) 0, *firstlist = (char *) 0, *firstel = (char *) 0,       \
               *secondl = (char *) 0, *secondlist = (char *) 0, *secondel = (char *) 0;    \
       char    *sfirstl, *ssecondl;                                          \
       size_t  rvclue=0;                                                     \
                                                                             \
       GET_DWORD_ARG(pattern, word);                                         \
       GET_FUNC_ARG(firstlist, word);                                       \
       GET_FUNC_ARG(secondlist, word);                                      \
                                                                             \
       firstl = get_variable(firstlist);                                     \
       secondl = get_variable(secondlist);                                   \
       sfirstl = firstl;                                                     \
       ssecondl = secondl;                                                   \
                                                                             \
       while ((firstel = next_func_arg(firstl, &firstl)))                     \
       {                                                                     \
               if (!(secondel = next_func_arg(secondl, &secondl)))            \
                       break;                                                \
                                                                             \
               if ((sense) == !wild_match((pat), (arg)))                     \
                       malloc_strcat_word_c(&booya, space, secondel, DWORD_DWORDS, &rvclue); \
       }                                                                     \
       new_free(&sfirstl);                                                   \
       new_free(&ssecondl);                                                  \
       RETURN_MSTR(booya);                                                   \
}
COPATFUNC(function_copattern, pattern, firstel, 0)
COPATFUNC(function_corpattern, firstel, pattern, 0)
COPATFUNC(function_cofilter, pattern, firstel, 1)
COPATFUNC(function_corfilter, firstel, pattern, 1)
#undef COPATFUNC


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
	where = my_atol((placeholder = function_findw(word))) + 1;
	new_free(&placeholder);

	if (where < 1)
		RETURN_EMPTY;

	placeholder = extractfw(lame, 1, where - 1);
	return placeholder;
}
		
/* Same as above, but includes the word being matched */
BUILT_IN_FUNCTION(function_tow, word)
{
	int     where;
	char	*lame = (char *) 0;
	char	*placeholder;

	lame = LOCAL_COPY(word);
	where = my_atol((placeholder = function_findw(word))) + 1;
	new_free(&placeholder);

	if (where < 1)
		RETURN_EMPTY;

	placeholder = extractfw(lame, 1, where);
	return placeholder;
}

/* Returns the string after the word being matched */
BUILT_IN_FUNCTION(function_afterw, word)
{
	int     where;
	char	*lame = (char *) 0;
	char	*placeholder;

	lame = malloc_strdup(word);
	placeholder = function_findw(word);
	where = my_atol(placeholder) + 1;

	new_free(&placeholder);

	if (where < 1)
	{
		new_free(&lame);
		RETURN_EMPTY;
	}
	placeholder = extractfw(lame, where + 1, EOS);
	new_free(&lame);
	return placeholder;
}

/* Returns the string starting with the word being matched */
BUILT_IN_FUNCTION(function_fromw, word)
{
	int     where;
	char 	*lame = (char *) 0;
	char	*placeholder;

	lame = malloc_strdup(word);
	placeholder = function_findw(word);
	where = my_atol(placeholder) + 1;

	new_free(&placeholder);

	if (where < 1)
	{
		new_free(&lame);
		RETURN_EMPTY;
	}

	placeholder = extractfw(lame, where, EOS);
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
	size_t	clue = 0;

	GET_FUNC_ARG(variable, word);
	GET_INT_ARG(start, word);
	GET_INT_ARG(length, word);

	old_value = get_variable(variable);
	num_words = count_words(old_value, DWORD_DWORDS, "\"");

	if (start < 0)
	{
		if ((length += start) <= 0)
			RETURN_EMPTY;
		start = 0;
	}

	if (start >= num_words)
	{
		left_part = malloc_strdup(old_value);
		middle_part = malloc_strdup(empty_string);
		right_part = malloc_strdup(empty_string);
	}

	else if (start + length >= num_words)
	{
		left_part = extractfw(old_value, 0, start - 1);
		middle_part = extractfw(old_value, start, EOS);
		right_part = malloc_strdup(empty_string);
	}

	else
	{
		left_part = extractfw(old_value, 0, start - 1);
		middle_part = extractfw(old_value, start, start + length - 1);
		right_part = extractfw(old_value, start + length, EOS);
	}

	malloc_strcat_wordlist_c(&new_value, space, left_part, &clue);
	malloc_strcat_wordlist_c(&new_value, space, word, &clue);
	malloc_strcat_wordlist_c(&new_value, space, right_part, &clue);

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
	RETURN_INT(number_on_channel(next_func_arg(word, &word), from_server));
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

	channel = next_func_arg(word, &word);
	if ((nicks = create_nick_list(channel, from_server)))
		return nicks;
	else
	{
		if (!nicks && (!word || !*word))
			RETURN_EMPTY;

		nicks = channel;
		channel = next_func_arg(word, &word);
		RETURN_INT(is_on_channel(channel, nicks) ? 1 : 0);
	}
}

BUILT_IN_FUNCTION(function_chops, word)
{
	char		*channel;

	channel = next_func_arg(word, &word);
	return create_chops_list(channel, from_server);
}

BUILT_IN_FUNCTION(function_nochops, word)
{
	char		*channel;

	channel = next_func_arg(word, &word);
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
		channel = next_func_arg(word, &word);
		if ((!channel || !*channel) && booya)
			break;

		key = get_channel_key(channel, from_server);
		malloc_strcat_word_c(&booya, space, (key && *key) ? key : "*", DWORD_DWORDS, &rvclue);
	}
	while (word && *word);

	RETURN_MSTR(booya);
}

/*
 * Based on a contribution made a long time ago by wintrhawk
 */
/* Worked over by jfn on 3/20/97.  If it breaks, yet all me. */
BUILT_IN_FUNCTION(function_channelmode, word)
{
	char	*channel;
	char    *booya = (char *) 0;
	const char	*mode;
	size_t	rvclue=0;

	do
	{
		channel = next_func_arg(word, &word);
		if ((!channel || !*channel) && booya)
			break;

		mode = get_channel_mode(channel, from_server);
		malloc_strcat_word_c(&booya, space, (mode && *mode) ? mode : "*", DWORD_DWORDS, &rvclue);
	}
	while (word && *word);

	RETURN_MSTR(booya);
}



BUILT_IN_FUNCTION(function_revw, words)
{
	char *booya = NULL;
	size_t  rvclue=0, wclue=0;

	while (words && *words)
		malloc_strcat_word_c(&booya, space, 
				last_arg(&words, &wclue, DWORD_DWORDS), 
				DWORD_DWORDS, &rvclue);

	RETURN_MSTR(booya);
}

BUILT_IN_FUNCTION(function_reverse, words)
{
	char *	retval;
	int	retval_size;
	unsigned char	*x, *y, *p, *r;

	retval_size = strlen(words);
	r = retval = new_malloc(retval_size + 1);

	/* Start at the end of the string */
	x = words + strlen(words);

	/* Walk back each code point from the end... */
	while (p = x, (previous_code_point(words, CUC_PP &x)))
	{
		/* Save our place */
		y = x;

		/* Copy all bytes for this code point */
		while (x < p)
			*r++ = *x++;

		/* Restore our place */
		x = y;
	}

	*r = 0;
	return retval;
}

/*
 * To save time, try to precalc the size of the output buffer.
 * jfn 03/18/98
 */
BUILT_IN_FUNCTION(function_jot, input)
{
	double  start 	= 0;
	double  stop 	= 0;
	double  interval = 1;
	double  counter;
	char	*booya 	= NULL;
	size_t	clue = 0;
	char	ugh[100];

        GET_FLOAT_ARG(start,input)
        GET_FLOAT_ARG(stop, input)
        if (input && *input)
                GET_FLOAT_ARG(interval, input)
        else
                interval = 1.0;

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
			snprintf(ugh, 99, "%f", counter);
			canon_number(ugh);
			malloc_strcat_word_c(&booya, space, ugh, DWORD_NO, &clue);
		}
	}
        else
	{
		for (counter = start; 
		     counter >= stop; 
		     counter -= interval)
		{
			snprintf(ugh, 99, "%f", counter);
			canon_number(ugh);
			malloc_strcat_word_c(&booya, space, ugh, DWORD_NO, &clue);
		}
	}

	RETURN_MSTR(booya);
}

char *function_shiftbrace (char *word)
{
	char    *value = (char *) 0;
	char    *var    = (char *) 0;
	char	*booya 	= (char *) 0;
	char    *placeholder;
	char 	*oof;

	GET_FUNC_ARG(var, word);
	if (!var || !*var)
		RETURN_EMPTY;

	placeholder = value = get_variable(var);
	if ((oof = next_expr(&value, '{')) == NULL)
	{
		new_free(&placeholder);
		RETURN_EMPTY;
	}
	booya = malloc_strdup(oof);
	add_var_alias(var, value, 0);

	new_free(&placeholder);
	RETURN_MSTR(booya);
}

char *function_shift (char *word)
{
	char    *value = (char *) 0;
	char    *var    = (char *) 0;
	char	*booya 	= (char *) 0;
	char *	free_it = NULL;

	GET_FUNC_ARG(var, word);
	upper(var);
	free_it = value = get_variable(var);

	booya = malloc_strdup(next_func_arg(value, &value));
	if (var)
		add_var_alias(var, value, 0);
	new_free(&free_it);
	RETURN_MSTR(booya);
}

/*
 * Usage: $unshift(<lval> <text>)
 * Note that <lval> and <text> are not <word> or <word list>, so the
 * rules governing double quotes and spaces and all that don't apply.
 * If <lval> has an illegal character, the false value is returned.
 */
char *function_unshift (char *word)
{
	char    *value = (char *) 0;
	char    *var    = (char *) 0;
	char	*booya  = (char *) 0;

	var = word;
	word = after_expando(word, 1, NULL);

	if (isspace(*word))
		*word++ = 0;
	/* If the variable has an illegal character, punt */
	else if (!*var || *word)
		RETURN_EMPTY;

	upper(var);
	value = get_variable(var);
	if (!word || !*word)
		return value;

	malloc_strcat_word(&booya, space, word, DWORD_DWORDS);
	malloc_strcat_wordlist(&booya, space, value);

	add_var_alias(var, booya, 0);
	new_free(&value);
	RETURN_MSTR(booya);
}

/*
 * Usage: $push(<lval> <text>)
 * Note that <lval> is an <lval> and not a <word> so it doesn't
 * honor double quotes and you can't have invalid characters in it.
 * If there is a syntax error, the false value is returned.  Note
 * that <text> is <text> and not <word list> so you can't put double
 * quotes in there, either.
 */
char *function_push (char *word)
{
	char    *value = (char *) 0;
	char    *var    = (char *) 0;

	var = word;
	word = after_expando(word, 1, NULL);

	if (isspace(*word))
		*word++ = 0;
	/* If the variable has an illegal character, punt */
	else if (*word)
		RETURN_EMPTY;

	upper(var);
	value = get_variable(var);
	if (!word || !*word)
		RETURN_MSTR(value);

	malloc_strcat_word(&value, space, word, DWORD_DWORDS);
	add_var_alias(var, value, 0);
	RETURN_MSTR(value);
}

char *function_pop (char *word)
{
	char *value	= (char *) 0;
	char *var	= (char *) 0;
	char *pointer	= (char *) 0;
	char *blech     = (char *) 0;
	size_t	cluep;
	char *free_it = NULL;

	GET_FUNC_ARG(var, word);
	upper(var);
	free_it = value = get_variable(var);
	if (!value || !*value)
	{
		new_free(&free_it);
		RETURN_EMPTY;
	}

	cluep = strlen(value);
	pointer = last_arg(&value, &cluep, DWORD_DWORDS);
	if (!value || cluep == 0)
		value = endstr(pointer);

	/* 
	 * because pointer points to value, we *must* make a copy of it
	 * *before* we free value! (And we cant forget to free value, either)
	 */
	blech = malloc_strdup(pointer);
	add_var_alias(var, value, 0);
	new_free(&free_it);
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
 * Rewritten again on Feb 14, 2005 based on function_msar.
 */
BUILT_IN_FUNCTION(function_sar, input)
{
	int	variable = 0, 
		global = 0, 
		case_sensitive = 0;
	int	delimiter;
	char *	last_segment;
	char *	text;
	char *	after;
	char *	workbuf = NULL;
	char *	search;
	char *	replace;

	/*
	 * Scan the leading part of the argument list, slurping up any
	 * options, and grabbing the delimiter character.  If we don't
	 * come across a delimiter, then just end abruptly.
	 */
	for (;; input++)
	{
		if (*input == 'r')
			variable = 1;
		else if (*input == 'g')
			global = 1;
		else if (*input == 'i')
			case_sensitive = 0;
		else if (*input == 'c')
			case_sensitive = 1;
		else if (!*input)
			RETURN_EMPTY;
		else
		{
			while ((delimiter = next_code_point(CUC_PP &input, 0)) == -1)
				input++;

			break;
		}
	}

	/*
	 * "input" contains a pair of strings like:
	 *	search<delim>replace[<delim>|<eol>]
	 *
	 * If we don't find a pair, then perform no substitution --
	 * just bail out right here.
	 */
	search = next_in_div_list(input, &after, delimiter);
	RETURN_IF_EMPTY(search);
	replace = next_in_div_list(after, &after, delimiter);
/*	RETURN_IF_EMPTY(replace); */

	/* 
	 * The last segment is either a text string, or a variable.  If it
	 * is a variable, look up its value.
	 */
	last_segment = after;
	RETURN_IF_EMPTY(last_segment);

	if (variable == 1) 
		text = get_variable(last_segment);
	else
		text = malloc_strdup(last_segment);

	workbuf = substitute_string(text, search, replace, 
					case_sensitive, global);
	new_free(&text);

        if (variable) 
                add_var_alias(last_segment, workbuf, 0);
	return workbuf;
}

BUILT_IN_FUNCTION(function_center, word)
{
	size_t  fieldsize,
		stringlen,
		cols,		/* Apparently "columns" is taken */
		pad;
	char    *padc;
	char	*copy;

	/* The width they want */
	GET_INT_ARG(fieldsize, word)

	/* 
	 * XXX This is copied from function_printlen(). 
	 * This should probably be a generic operation.
	 */
        copy = new_normalize_string(word, 2, NORMALIZE); 
        cols = output_with_count(copy, 0, 0);
	stringlen = strlen(copy);
        new_free(&copy);

	/* The string is wider than the field, just return it */
	if (cols > fieldsize)
		RETURN_STR(word);

	/*
	 * Calculate how much space we need.
	 * For a string N columns wide, centered in a field of length X,
	 * N / 2 is the size of the string in each half of the field;
	 * (X - N) / 2 is the size of the space in each half of the field;
	 * Therefore, the size of the field is:
	 *      N + ((X - N) / 2)
	 *
	 * Since the string may be utf8, we must use the column count to
	 * determine how many bytes to add; but we must use the actual 
	 * string length to decide how big the result is.
	 */
	pad = stringlen + ((fieldsize - cols) / 2);
	padc = (char *)new_malloc(pad + 1);
	snprintf(padc, pad + 1, "%*s", (int)pad, word);  /* Right justify it */
	return padc;
}

/* 
 * Usage: $fix_width(<width> <l, c, r> "<fill char>" <string>)
 * Returns: A string exactly <width> columns wide.
 * Arguments:
 *   $0  - How many columns the resulting string should be
 *   $1  - Justification - left, center, or right
 *   $2  - Character to use if the string is too short (always a dword)
 *   $3- - The string to format
 *
 * Note: The resulting string is always exactly <width> columns long.
 * If the string is too long, it will be truncated in accordance with
 * the justification (ie, 'l' means the end of the string is truncated)
 */
BUILT_IN_FUNCTION(function_fix_width, word)
{
        int 	width;
        char *	justify;
	int	justifynum = -1;
	char *	fillchar_str;
	int	fillchar;
        char *	retval;

        GET_INT_ARG(width, word);
	if (width < 0 || !*word)
                RETURN_EMPTY;

        GET_FUNC_ARG(justify, word);
	if (!my_strnicmp(justify, "left", 1))
		justifynum = -1;
	else if (!my_strnicmp(justify, "center", 1))
		justifynum = 0;
	else if (!my_strnicmp(justify, "right", 1))
		justifynum = 1;
	else
		RETURN_EMPTY;

	GET_DWORD_ARG(fillchar_str, word);
	fillchar = next_code_point(CUC_PP &fillchar_str, 1);

	retval = fix_string_width(word, justifynum, fillchar, width, 1);
	RETURN_MSTR(retval);
}

/*
 * Why does this look so much like cpindex?
 * Because code points can take up different sizes.
 * So if the user tries to split on a 2-byte code point, we can't
 * just replace it with one space -- or two spaces -- in place!
 * No, we have to copy the whole string, byte by byte to do it right.
 */
BUILT_IN_FUNCTION(function_split, word)
{
	unsigned char 	*search;
	int		inverted = 0;
	unsigned char 	*retval, *r;
const	unsigned char	*p, *s;
	int		c, d;
	int		found;

	/* 
	 * What chars does the user want converted to space? 
	 * This has "sindex" semantics -- so a leading ^ inverts the set 
	 */
	GET_DWORD_ARG(search, word)
	if (*search == '^')
	{
		inverted = 1;
		search++;
	}

	/* The return value will not be shorter than the input */
	r = retval = new_malloc(strlen(word) + 1);

	/* For each code point in the source string... */
	p = word;
	while ((c = next_code_point(&p, 1)))
	{
		/* Look for that same code point in the search string */
		found = 0;
		s = search;
		while ((d = next_code_point(&s, 1)))
		{
			if (c == d)
			{
				found = 1;
				break;
			}
		}

		/* If we found a match, put a space here. */
		if (found != inverted)
			*r++ = ' ';
		/* Otherwise, copy the code point over */
		else
		{
			unsigned char utf8str[16];
			char *x;

			ucs_to_utf8(c, utf8str, sizeof(utf8str));
			for (x = utf8str; *x; x++)
				*r++ = *x;
		}
	}

	*r = 0;
	return retval;
}

/*
 * This function converts an input word list into a string, where each word
 * of the input string must be a cardinal number in base 10 (0-255) or base 16
 * (0x00-0xFF -- including the leading "0x"!).  The output string is the 
 * sequence of bytes represented by the catenation of each of these cardinal
 * numbers.
 *
 * An invalid input word will be converted to 0 by my_atol() which results in
 * a zero byte, which terminates the string.  Therefore, any invalid word
 * in the input implicitly ends the return value.  You should not depend on
 * this behavior because in the future invalid words may be skipped rather than
 * converted to nul bytes.
 *
 * Example:
 * $chr(104 105 33) returns "hi!"
 */
BUILT_IN_FUNCTION(function_chr, word)
{
	char *	aboo = NULL;
	char *	ack;
	char *	blah;
	size_t	bytes = 0;
	size_t	cnt;
	char 	*s, *x;
	unsigned char 	utf8str[8];
	int	code_point;

	cnt = count_words(word, DWORD_DWORDS, "\"");
	ack = aboo = new_malloc(cnt * 6 + 7);

	while ((blah = next_func_arg(word, &word)))
	{
		if (blah[0] == 'U' && blah[1] == '+')
		{
			blah += 2;
			code_point = strtol(blah, &x, 16);

			/* If it's invalid, skip it */
			if (blah == x)
				continue;

			/* Otherwise, convert to utf8 */
			ucs_to_utf8(code_point, utf8str, sizeof(utf8str));
			for (x = utf8str; *x; x++)
			{
				*ack++ = *x;
				bytes++;
			}
		}
		else
		{
			*ack++ = (char)my_atol(blah);
			bytes++;
		}

		if (bytes >= (cnt * 6))
			break;
	}

	*ack = 0;
	RETURN_MSTR(aboo);
}

/*
 * This function converts an input word list into a string, where each word
 * of the input string must be a cardinal number in base 10 (0-255) or base 16
 * (0x00-0xFF -- including the leading "0x"!).  The output string is the CTCP
 * ENQUOTED  sequence of bytes represented by the catenation of each of these 
 * cardinal numbers.
 *
 * An invalid input word will be converted to 0 by my_atol() which results in
 * a zero byte.  You should not depend on this behavior because in the future 
 * invalid words may be skipped rather than converted to nul bytes.
 *
 * Example:
 * $chr(104 105 33 0 134 104 105 33 0) returns "hi\0\\hi!\0"
 *	because the string undergoes CTCP QUOTING before being returned.
 *
 * XXX This is a clone of function_chr() except for the transformation
 * at the end.  How bogus.
 */
BUILT_IN_FUNCTION(function_chrq, word)
{
	char *	aboo = NULL;
	char *	ack;
	char *	blah;
	size_t	bytes = 0;
	size_t	cnt;
	char 	*s, *x;
	unsigned char 	utf8str[8];
	int	code_point;
	char	*ret;

	cnt = count_words(word, DWORD_DWORDS, "\"");
	ack = aboo = new_malloc(cnt * 6 + 7);

	while ((blah = next_func_arg(word, &word)))
	{
		if (blah[0] == 'U' && blah[1] == '+')
		{
			blah += 2;
			code_point = strtol(blah, &x, 16);

			/* If it's invalid, skip it */
			if (blah == x)
				continue;

			/*
			 * As a SPECIAL CASE code point 0 gets copied.
			 */
			if (code_point == 0)
			{
				*ack++ = 0;
				bytes++;
			}
			
			/* Otherwise, convert to utf8 */
			else
			{
			    ucs_to_utf8(code_point, utf8str, sizeof(utf8str));
			    for (x = utf8str; *x; x++)
			    {
				*ack++ = *x;
				bytes++;
			    }
			}
		}
		else
		{
			*ack++ = (char)my_atol(blah);
			bytes++;
		}

		if (bytes >= (cnt * 6))
			break;
	}

	*ack = 0;
	ret = transform_string_dyn("+CTCP", aboo, bytes, NULL);
	new_free(&aboo);

	RETURN_MSTR(ret);
}

/*
 * This function converts an input string into a word list, where each byte
 * of the input string is converted to a cardinal number in base 10 (0-255)
 * and the return value is a word list of such cardinal numbers.
 *
 * Example:
 * $ascii(hi!) returns "104 105 33"
 */
BUILT_IN_FUNCTION(function_ascii, word)
{
	char *aboo = NULL;
	size_t  rvclue=0;

	if (!word || !*word)
		RETURN_EMPTY;

	for (; *word; ++word)
		malloc_strcat_wordlist_c(&aboo, space, ltoa((long)(unsigned char)*word), &rvclue);

	return aboo;
}

/*
 * This function converts an input string into a word list, where each 
 * unicode code point of the input string is converted to a unicode code 
 * point description ("U+xxxx") which you could pass back to $chr(),
 * and the return value is a word list of such descriptions.
 * Invalid code points are discarded.
 *
 * Example:
 * $unicode(hi!) returns "U+0068 U+0069 U+0021"
 */
BUILT_IN_FUNCTION(function_unicode, word)
{
	char *	aboo = NULL;
	size_t  rvclue=0;
	const unsigned char *s, *x;
	int	code_point;
	char	result[8];

	if (!word || !*word)
		RETURN_EMPTY;

	s = word;
	while ((code_point = next_code_point(&s, 0)))
	{
		/* Skip invalid bytes/sequences */
		if (code_point == -1)
			continue;
		snprintf(result, sizeof(result), "U+%04X", code_point);
		malloc_strcat_wordlist_c(&aboo, space, result, &rvclue);
	}

	return aboo;
}

/*
 * This function converts a CTCP ENCODED input string into a word list, where
 * each byte of the input string is converted to a cardinal number in base 10
 * (0-255) and the return value is a word list of such cardinal numbers.
 *
 * Example:
 * $ascii(hi!) returns "104 105 33"   of course
 * $ascii(hi!\0\\hi!\0)  returns "104 105 33 0 134 104 105 33 0"
 *	because the string undergoes CTCP DEQUOTING first.
 *	-- Be careful: the CTCP QUOTE character (\) is the same as ircII's
 *	   quote character, so you can quickly get into quoting hell here.
 */
BUILT_IN_FUNCTION(function_asciiq, word)
{
	char	*aboo = NULL, *free_it;
	size_t	rvclue=0;

	if (!word || !*word)
		RETURN_EMPTY;

	/* The CTCP enquoting is intentional... */
	free_it = word = transform_string_dyn("-CTCP", word, 0, NULL);
	RETURN_IF_EMPTY(word);

	for (; word && *word; word++)
	{
		malloc_strcat_wordlist_c(&aboo, space, 
				ltoa((long)(unsigned char)*word), &rvclue);
	}

	new_free(&free_it);
	return aboo;
}

BUILT_IN_FUNCTION(function_which, word)
{
	char *file1;
	Filename result;
	const char *path;

	GET_DWORD_ARG(file1, word);
	if (word && *word)
		path = word;
	else
		path = get_string_var(LOAD_PATH_VAR);

	if (path_search(file1, path, result))
		RETURN_EMPTY;

	RETURN_STR(result);
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
	GET_DWORD_ARG(filename, words);
	GET_FUNC_ARG(words, words); /* clobbers remaining args */

	if (!words || !*words)
		RETURN_EMPTY;
	else if (!my_stricmp(words, "R"))
		RETURN_INT(open_file_for_read(filename));
	else if (!my_stricmp(words, "W"))
		RETURN_INT(open_file_for_write(filename, "a"));
	else
		RETURN_INT(open_file_for_write(filename, lower(words)));
}

BUILT_IN_FUNCTION(function_close, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_close(my_atol(next_func_arg(words, &words))));
}	

BUILT_IN_FUNCTION(function_write, words)
{
	char *	fdc;
	char 	target[1024];
	int	retval;

	GET_FUNC_ARG(fdc, words);
	if (is_number(fdc) || 
		( (*fdc == 'w' || *fdc == 'W' || 
		   *fdc == 'l' || *fdc == 'L' ) &&
			is_number(fdc + 1)))
	{
		snprintf(target, sizeof target, "@%s", fdc);
		retval = target_file_write(target, words);
		RETURN_INT(retval);
	}
	RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_writeb, words)
{
	int	retval = 0;
	char *	fdc;

	GET_FUNC_ARG(fdc, words);

	/* We don't use target_file_write() for biary data. */

	if (is_number(fdc))
		retval = file_writeb(0, my_atol(fdc), words);
	else if ((*fdc == 'w' || *fdc == 'W') && is_number(fdc + 1))
		retval = file_writeb(1, my_atol(fdc + 1), words);
	else if ((*fdc == 'l' || *fdc == 'L') && is_number(fdc + 1))
		retval = file_writeb(2, my_atol(fdc + 1), words);
	else
		RETURN_EMPTY;

	RETURN_INT(retval);
}

BUILT_IN_FUNCTION(function_read, words)
{
	char *fdc = NULL, *numb = NULL;

	GET_FUNC_ARG(fdc, words);
	if (words && *words)
		GET_FUNC_ARG(numb, words);

	if (numb)
		return file_readb(my_atol(fdc), my_atol(numb));
	else
		return file_read(my_atol(fdc));
}

BUILT_IN_FUNCTION(function_seek, words)
{
	int	fdc;
	off_t	numb;
	char *	whence = NULL;

	GET_INT_ARG(fdc, words);
	GET_INT_ARG(numb, words);
	GET_FUNC_ARG(whence, words);

	RETURN_INT(file_seek(fdc, numb, whence));
}

BUILT_IN_FUNCTION(function_eof, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_eof(my_atol(next_func_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_error, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_error(my_atol(next_func_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_skip, words)
{
	int arg1, arg2 = 1;
	GET_INT_ARG(arg1, words);
	if (words && *words)
		GET_INT_ARG(arg2, words);
	RETURN_INT(file_skip(arg1, arg2));
}

BUILT_IN_FUNCTION(function_tell, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_tell(my_atol(next_func_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_isfilevalid, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_valid(my_atol(next_func_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_rewind, words)
{
	RETURN_IF_EMPTY(words);
	RETURN_INT(file_rewind(my_atol(next_func_arg(words, &words))));
}

BUILT_IN_FUNCTION(function_ftruncate, words)
{
	off_t	length;
	char	*ret = NULL;

	GET_INT_ARG(length, words);
	if (truncate(words, length))
		ret = strerror(errno);
	RETURN_STR(ret);
}

BUILT_IN_FUNCTION(function_iptoname, words)
{
	char	ret[256];

	*ret = 0;
	inet_ptohn(AF_UNSPEC, words, ret, sizeof(ret));
	RETURN_STR(ret);		/* Dont put function call in macro! */
}

BUILT_IN_FUNCTION(function_nametoip, words)
{
	char	ret[256];

	*ret = 0;
	inet_hntop(AF_INET, words, ret, sizeof(ret));
	RETURN_STR(ret);		/* Dont put function call in macro! */
}

BUILT_IN_FUNCTION(function_convert, words)
{
	char	ret[256];

	*ret = 0;
	one_to_another(AF_INET, words, ret, sizeof(ret));
	RETURN_STR(ret);		/* Dont put function call in macro! */
}

/*
 * $tr([delim][chars-out][delim][chars-in][delim][text])
 *
 * For the string [text], replace any instances of any character
 * in [chars-out] with the corresponding character in [chars-in].
 *
 * Example:
 *	$tr(/abc/def/happy happy job job/)
 * Means apply these transforms:
 *	a -> d
 *	b -> e
 *	c -> f
 * So the result is:
 *	hdppy hdppy joe joe
 *
 * This is *NOT* case insensitive, like you might expect.  Oh well!
 */

BUILT_IN_FUNCTION(function_translate, input)
{
	int		delim, codepoint;
	unsigned char 	*s, *p;
	unsigned char	*chars_in, *chars_out, *text;
	char		*retval, *r;
	int		char_out, char_in, final_char_in;
	unsigned char 	utf8str[16];

	RETURN_IF_EMPTY(input);

	/* 
	 * The first CP we see is the delimiter. 
	 * By convention, this is a slash ('/'), but it doesn't have to be.
	 */
	s = input;
	while ((delim = next_code_point(CUC_PP &s, 0)) == -1)
		s++;

	/*
	 * The chars to swap out begin after the delim.
	 */
	chars_out = s;

	/*
	 * Now we look for the next delim...
	 */
	for (;;)
	{
		p = s;
		while ((codepoint = next_code_point(CUC_PP &s, 0)) == -1)
			s++;
		if (codepoint == 0)
			RETURN_EMPTY;

		/* 
		 * Since the delim might be multiple bytes,
		 * we keep a pointer to the start of each CP.
		 * If we find the delim, we null out the first
		 * byte.
		 */
		if (codepoint == delim)
		{
			*p = 0;
			break;
		}
	}

	/*
	 * The chars to swap in begin after the delim.
	 */
	chars_in = s;

	/*
	 * Now we look for the final delim...
	 */
	for (;;)
	{
		p = s;
		while ((codepoint = next_code_point(CUC_PP &s, 0)) == -1)
			s++;
		if (codepoint == 0)
			RETURN_EMPTY;

		/* 
		 * Since the delim might be multiple bytes,
		 * we keep a pointer to the start of each CP.
		 * If we find the delim, we null out the first
		 * byte.
		 */
		if (codepoint == delim)
		{
			*p = 0;
			break;
		}
	}

	text = s;

	/*
	 * The worst case is replacing every byte in 'text' with a 
	 * 6 byte utf8 code point.  
	 */
	r = retval = new_malloc(strlen(text) * 6 + 6);

	/*
	 * Now we walk every code point in text, deciding what to do.
	 */
	for (;;)
	{
		while ((codepoint = next_code_point(CUC_PP &text, 0)) == -1)
			text++;

		/* The only way out is when we get the final nul. */
		if (codepoint == 0)
		{
			*r = 0;
			RETURN_MSTR(retval);
		}

		/*
		 * For each byte, determine whether it is in the 'chars_out'
		 * string.  If it is, replace it with the corresponding
		 * byte in 'chars_in'
		 *
		 * XXX IMPORTANT!  We depend strongly on next_code_point()
		 * refusing to walk off the end of a string here to handle
		 * cases where the chars_out is shorter than chars_in.
		 * The effect is that the "replaced" character is a nul,
		 * which removes the character.  If next_code_point() ever
		 * stops doing that, this will need to be rewritten.
		 */
		s = chars_out;
		p = chars_in;
		final_char_in = 0;
		for (;;)
		{
			char_out = next_code_point(CUC_PP &s, 1);
			char_in = next_code_point(CUC_PP &p, 1);

			/*
			 * The historical (and defined) behavior is that
			 * every character in 'char_out' maps to a character
			 * in 'char_in' and whenever 'char_in' is shorter
			 * than 'char_out', it shall be extended with its 
			 * final character.
			 *
			 * For example:
			 *	$tr(/abcde/xyz/tasty cakes/)
			 * behaves as though it were:
			 *	$tr(/abcde/xyzzz/tasty cakes/)
			 */
			if (char_in == 0)
				char_in = final_char_in;
			else
				final_char_in = char_in;

			/* 
			 * This CP was not in 'char_out' -- copy it.
			 */
			if (char_out == 0)
				break;
			/*
			 * The CP was found -- change it to the right one.
			 */
			else if (codepoint == char_out)
			{
				codepoint = char_in;
				break;
			}
		}

		/*
		 * Convert the code point back to a string and copy it in.
		 */
		ucs_to_utf8(codepoint, utf8str, sizeof(utf8str));
		s = utf8str;
		while (*s)
			*r++ = *s++;
	}
}

BUILT_IN_FUNCTION(function_server_version, word)
{
	RETURN_STR("2.8");
}

BUILT_IN_FUNCTION(function_unlink, words)
{
	Filename expanded;
	int 	failure = 0;

	while (words && *words)
	{
		char *fn = new_next_arg(words, &words);
		if (!fn || !*fn)
			fn = words, words = NULL;
		if (normalize_filename(fn, expanded))
			failure++;
		else if (unlink(expanded))
			failure++;
	}

	RETURN_INT(failure);
}

BUILT_IN_FUNCTION(function_rename, words)
{
	char *	filename1,
	     *	filename2;
	Filename expanded1;
	Filename expanded2;

	GET_DWORD_ARG(filename1, words)
	if (normalize_filename(filename1, expanded1))
		RETURN_INT(-1);

	GET_DWORD_ARG(filename2, words)
	if (normalize_filename(filename2, expanded2))
		RETURN_INT(-1);

	RETURN_INT(rename(expanded1, expanded2));
}

BUILT_IN_FUNCTION(function_rmdir, words)
{
	Filename expanded;
	int 	failure = 0;

	while (words && *words)
	{
		char *fn = new_next_arg(words, &words);
		if (!fn || !*fn)
			fn = words, words = NULL;
		if (normalize_filename(fn, expanded))
			failure++;
		else if (rmdir(expanded))
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

		snprintf(format, sizeof format, "%%.%de", -num-1);
		snprintf(buffer, sizeof buffer, format, value);
		foo = atof(buffer);
		snprintf(buffer, sizeof buffer, "%f", foo);
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
		snprintf(format, sizeof format, "%%10.%dlf", num);
		snprintf(buffer, sizeof buffer, format, value);
	}
	else
		RETURN_EMPTY;

	while (*buffer && isspace(*buffer))
		ov_strcpy(buffer, buffer+1);

	RETURN_STR(buffer);
}


/*
 * Apprantly, this was lifted from a CS client.  I reserve the right
 * to replace this code in future versions. 
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
	size_t	size;

	GET_INT_ARG(ltime, input);

	seconds = ltime % 60;
	ltime = (ltime - seconds) / 60;
	minutes = ltime%60;
	ltime = (ltime - minutes) / 60;
	hours = ltime % 24;
	days = (ltime - hours) / 24;
	tstr = tmp;
	size = sizeof tmp;

	if (days)
	{
		snprintf(tstr, size, "%ldd ", (long)days);
		size -= strlen(tstr);
		tstr += strlen(tstr);
	}
	if (hours)
	{
		snprintf(tstr, size, "%ldh ", (long)hours);
		size -= strlen(tstr);
		tstr += strlen(tstr);
	}
	if (minutes)
	{
		snprintf(tstr, size, "%ldm ", (long)minutes);
		size -= strlen(tstr);
		tstr += strlen(tstr);
	}
	if (seconds || (!days && !hours && !minutes))
		snprintf(tstr, size, "%lds", (long)seconds);
	else
		*--tstr = 0;	/* chop off that space! */

	RETURN_STR(tmp);
}

/* 
 * The idea came from a CS client.  I wrote this from scratch, though.
 */
BUILT_IN_FUNCTION(function_utime, input)
{
	Timeval tp;

	get_time(&tp);
	return malloc_sprintf(NULL, INTMAX_FORMAT" %ld", (intmax_t)tp.tv_sec, 
							 (long)tp.tv_usec);
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

BUILT_IN_FUNCTION(function_numwords, input)
{
	RETURN_INT(count_words(input, DWORD_DWORDS, "\""));
}

BUILT_IN_FUNCTION(function_strlen, input)
{
	RETURN_INT(quick_code_point_count(input));
}

BUILT_IN_FUNCTION(function_aliasctl, input)
{
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
        Filename expanded;
	char	*filename;

	if (!(filename = new_next_arg(words, &words)))
		filename = words;

	if (!filename || !*filename)
		RETURN_INT(-1);

	if (normalize_filename(filename, expanded))
		RETURN_INT(-1);

	if (access(expanded, R_OK) == -1)
		RETURN_INT(-1);

	RETURN_INT(1);
}

BUILT_IN_FUNCTION(function_fsize, words)
{
	Filename expanded;
	char *	filename;

	if (!(filename = new_next_arg(words, &words)))
		filename = words;

	if (!filename || !*filename)
		RETURN_INT(-1);

	if (normalize_filename(filename, expanded))
		RETURN_INT(-1);

	RETURN_INT(file_size(expanded));
}

/* 
 * Contributed by CrowMan
 * Updated by caf to allow selection of any hashing method supported by the
 * system.
 */
/*
 * $crypt(password salt)
 * 
 * This returns a password hash when given a salt and password.  The function
 * supports all hashing methods supported by the local C library, with the
 * hashing method selected by the format of the salt string.  See your local
 * crypt(3) man page for details.
 *
 * All systems (and older versions of EPIC) support the classic DES-based UNIX
 * hashing function, which is selected by a two-character salt in the range 
 * [a-zA-Z0-9./].  This method returns a 13-char string and truncates the
 * supplied password to 8 characters.  It is also comically weak in the modern
 * era.
 *
 * Returns an empty string if one or both args missing, or if crypt() fails.
 *
 * Credits: Thanks to Strongbow for showing me how crypt() works.
 * Written by: CrowMan
 */
BUILT_IN_FUNCTION(function_crypt, words)
{
	/* Some systems use (char *) and some use (const char *) as arguments. */
	extern char *crypt(); 
	char *pass, *salt;
	char *ret;

	GET_DWORD_ARG(pass, words)
	GET_DWORD_ARG(salt, words)
	ret = crypt(pass, salt);
	RETURN_STR(ret);		/* Dont call function in macro! */
}

BUILT_IN_FUNCTION(function_info, words)
{
	char *which;
	extern char *info_c_sum;

	GET_FUNC_ARG(which, words);

	     if (!my_strnicmp(which, "C", 1))
		RETURN_STR(compile_info);
	else if (!my_strnicmp(which, "O", 1))
		RETURN_STR(compile_time_options);
	else if (!my_strnicmp(which, "S", 1))
		RETURN_STR(info_c_sum);
	else if (!my_strnicmp(which, "W", 1))
		RETURN_INT(1);
	else if (!my_strnicmp(which, "M", 1))
	{
		if (get_int_var(OLD_MATH_PARSER_VAR))
			RETURN_INT(0);
		RETURN_INT(1);
	}
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
        const char *refnum;
        int  col, li;
	int	window;

        if (!words || !*words)
                refnum = zero;
        else
                GET_FUNC_ARG(refnum, words);

	if ((window = lookup_window(refnum) < 1))
                RETURN_EMPTY;
        if (get_window_geometry(window, &col, &li))
                RETURN_EMPTY;

        return malloc_sprintf(NULL, "%d %d", col, li);
}

/*
 * This is basically the opposite of $strip().
 * The only difference is the "found" versus "!found"
 * XXX What a shame to have two functions differing by only one byte.
 */
BUILT_IN_FUNCTION(function_pass, input)
{
	char *	search;
	const unsigned char 	*s, *p;
	int	c, d;
	int	found;
	char *	result, *r;

	GET_DWORD_ARG(search, input);
	RETURN_IF_EMPTY(input);

	r = result = (char *)new_malloc(strlen(input) + 1);

	p = input;
	while ((c = next_code_point(&p, 1)))
	{
		found = 0;
		s = search;
		while ((d = next_code_point(&s, 1)))
		{
			if (c == d)
			{
				found = 1;
				break;
			}
		}
		if (found)
		{
			unsigned char utf8str[16];
			unsigned char *x;

			ucs_to_utf8(c, utf8str, sizeof(utf8str));
			for (x = utf8str; *x; x++)
				*r++ = *x;
		}
	}
	*r = 0;
	return result;		/* DONT USE RETURN_STR HERE! */
}

BUILT_IN_FUNCTION(function_repeat, words)
{
	int	num;
	size_t	size;
	char *	retval = NULL;
	size_t	clue;

	/*
	 * XXX - This is a brutal, unmerciful, and heinous offense
	 * against all that is good and right in the world.  However,
	 * backwards compatability requires that $repeat(10  ) work,
	 * even though that violates standard practice of word extraction.
	 *
	 * This replaces a GET_INT_ARG(num, words)
	 */
	num = strtoul(words, &words, 10);
	if (words && *words)
		words++;

	if (num < 1)
		RETURN_EMPTY;

	if (!words)
		RETURN_EMPTY;

	size = strlen(words) * num + 1;

	/* 
	 * Don't allow the return value to be > 1MB to avoid
	 * "out of memory failure" panic
	 */
	if (size > 1000000)
	{
		num = 1000000 / strlen(words);
		size = strlen(words) * num + 1;
	}

	retval = (char *)new_malloc(size);
	*retval = 0;
	clue = 0;

	for (; num > 0; num--)
		strlcat_c(retval, words, size, &clue);

	return retval;
}

BUILT_IN_FUNCTION(function_epic, words)
{
	RETURN_INT(1);
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

static int sort_it (const void *val1, const void *val2)
{
	return my_stricmp(*(const char * const *)val1, *(const char * const *)val2);
}

BUILT_IN_FUNCTION(function_sort, words)
{
	int 	wordc;
	char **wordl;
	char	*retval;

	if (!(wordc = splitw(words, &wordl, DWORD_DWORDS)))
		RETURN_EMPTY;

	qsort((void *)wordl, wordc, sizeof(char *), sort_it);
	retval = unsplitw(&wordl, wordc, DWORD_DWORDS);
	RETURN_MSTR(retval);
}

static int num_sort_it (const void *val1, const void *val2)
{
	const char *oneptr = *(const char * const *)val1;
	const char *twoptr = *(const char * const *)val2;
	char *oneptr_result;
	char *twoptr_result;
	intmax_t v1, v2;

	while (*oneptr && *twoptr)
	{
		while (*oneptr && *twoptr && !(my_isdigit(oneptr)) && !(my_isdigit(twoptr)))
		{
			char cone = tolower(*oneptr);
			char ctwo = tolower(*twoptr);
			if (cone != ctwo)
				return (cone - ctwo);
			oneptr++, twoptr++;
		}

		if (!*oneptr || !*twoptr)
			break;

		/* These casts discard 'const', but do i care? */
		v1 = strtoimax(oneptr, &oneptr_result, 0);
		v2 = strtoimax(twoptr, &twoptr_result, 0);
		if (v1 != v2)
			return v1 - v2;
		oneptr = oneptr_result;
		twoptr = twoptr_result;
	}
	return (*oneptr - *twoptr);
}

BUILT_IN_FUNCTION(function_numsort, words)
{
	int wordc;
	char **wordl;
	char *retval;

	if (!(wordc = splitw(words, &wordl, DWORD_DWORDS)))
		RETURN_EMPTY;
	qsort((void *)wordl, wordc, sizeof(char *), num_sort_it);
	retval = unsplitw(&wordl, wordc, DWORD_DWORDS);
	RETURN_MSTR(retval);
}


BUILT_IN_FUNCTION(function_notify, words)
{
	int showon = -1, showserver = from_server;
	char *firstw;

	while (words && *words)
	{
		firstw = next_func_arg(words, &words);
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
#ifndef GLOB_INSENSITIVE
#define GLOB_INSENSITIVE 0
#endif

BUILT_IN_FUNCTION(function_glob, word)
{
	char 	*path;
	Filename path2;
	char	*retval = NULL;
	int 	numglobs, i;
	size_t	rvclue=0;
	glob_t 	globbers;

	memset(&globbers, 0, sizeof(glob_t));
	while (word && *word)
	{
		char	*freepath = NULL;

		GET_DWORD_ARG(path, word);
		if (!path || !*path)
			path = word, word = NULL;
		else
		{
		   /* The CTCP enquoting is intentional, probably
		    * to do de-\-ing */
		    freepath = path = transform_string_dyn("-CTCP", path, 0, NULL);
		    RETURN_IF_EMPTY(path);
		}
		expand_twiddle(path, path2);

		if ((numglobs = glob(path2, GLOB_MARK | GLOB_QUOTE | GLOB_BRACE,
						NULL, &globbers)) < 0)
			RETURN_INT(numglobs);

		for (i = 0; i < globbers.gl_pathc; i++)
		{
			if (strpbrk(globbers.gl_pathv[i], " \""))
			{
				size_t size;
				char *b;

				size = strlen(globbers.gl_pathv[i]) + 4;
				b = alloca(size);
				snprintf(b, size, "\"%s\"", 
						globbers.gl_pathv[i]);
				malloc_strcat_wordlist_c(&retval, space, b, &rvclue);
			}
			else
				malloc_strcat_wordlist_c(&retval, space, globbers.gl_pathv[i], &rvclue);
		}
		globfree(&globbers);
		new_free(&freepath);
	}

	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_globi, word)
{
	char 	*path;
	Filename path2;
	char	*retval = NULL;
	int 	numglobs, i;
	size_t	rvclue=0;
	glob_t 	globbers;

	memset(&globbers, 0, sizeof(glob_t));
	while (word && *word)
	{
		char	*freepath = NULL;

		GET_DWORD_ARG(path, word);
		if (!path || !*path)
			path = word, word = NULL;
		else
		{
		   /* The CTCP enquoting is intentional, probably
		    * to do de-\-ing */
		    freepath = path = transform_string_dyn("-CTCP", path, 0, NULL);
		    RETURN_IF_EMPTY(path);
		}
		expand_twiddle(path, path2);

		if ((numglobs = bsd_glob(path2,
				GLOB_MARK | GLOB_INSENSITIVE | GLOB_QUOTE | GLOB_BRACE, 
				NULL, &globbers)) < 0)
			RETURN_INT(numglobs);

		for (i = 0; i < globbers.gl_pathc; i++)
		{
			if (strpbrk(globbers.gl_pathv[i], " \""))
			{
				size_t size;
				char *b;

				size = strlen(globbers.gl_pathv[i]) + 4;
				b = alloca(size);
				snprintf(b, size, "\"%s\"", 
						globbers.gl_pathv[i]);
				malloc_strcat_wordlist_c(&retval, space, b, &rvclue);
			}
			else
				malloc_strcat_wordlist_c(&retval, space, globbers.gl_pathv[i], &rvclue);
		}
		bsd_globfree(&globbers);
		new_free(&freepath);
	}

	RETURN_MSTR(retval);
}


BUILT_IN_FUNCTION(function_mkdir, words)
{
	Filename expanded;
	int 	failure = 0;

	while (words && *words)
	{
		char *fn = new_next_arg(words, &words);
		if (!fn || !*fn)
			fn = words, words = NULL;
		if (normalize_filename(fn, expanded))
			failure++;
		if (mkdir(expanded, 0777))
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
	Filename expanded;

	GET_DWORD_ARG(filearg, words);
	fd = (int) strtoul(filearg, &after, 0);

	GET_FUNC_ARG(perm_s, words);
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
		if (normalize_filename(filearg, expanded))
			RETURN_INT(-1);

		RETURN_INT(chmod(expanded, perm));
	}
}

BUILT_IN_FUNCTION(function_twiddle, words)
{
	Filename retval;

	*retval = 0;
	expand_twiddle(words, retval);
	RETURN_STR(retval);
}


static int unsort_it (const void *v1, const void *v2)
{
	/* This just makes me itch. ;-) */
	return (int)(*(const char * const *)v1 - *(const char * const *)v2);
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
        char    **list = NULL;
	char *booya = NULL;
        int     listc, listi, listo;

	RETURN_IF_EMPTY(word);
        listc = splitw(word, &list, DWORD_DWORDS);

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
				*(char *)list[listo] = 0;
				listo = listi;
			} else {
				*(char *)list[listi] = 0;
			}
		}
	}

	/* We want the remaining words to appear in their original order */
	qsort((void *)list, listc, sizeof(char *), unsort_it);
	booya = unsplitw(&list, listc, DWORD_DWORDS);

#else
    { /* Oh hush.  It is #ifdef'd out, after all. */
	char	*tval;
	char	*input;
	size_t	rvclue=0;
	size_t	size;

	/*
	 * XXX This algorithm is too expensive.  It should be done
	 * by some other way.  Calling function_findw() is a hack.
	 */

	/* The first word is always obviously included. */
	booya = malloc_strdup(list[0]);
        for (listi = 1; listi < listc; listi++)
        {
		size = strlen(list[listi]) + strlen(booya) + 2;
		input = alloca(size);
		strlopencat_c(input, size, NULL, list[listi], space, booya, NULL);

		tval = function_findw(input);
		if (my_atol(tval) == -1)
			malloc_strcat_word_c(&booya, space, list[listi], DWORD_DWORDS, &rvclue);
		new_free(&tval);
        }
    }
#endif

        new_free((char **)&list);
	RETURN_MSTR(booya);
}

BUILT_IN_FUNCTION(function_status, word)
{
	unsigned 	window_refnum;
	int 		status_line;
	char *		retval;

	GET_INT_ARG(window_refnum, word);
	GET_INT_ARG(status_line, word);
	retval = get_window_status_line(window_refnum, status_line);
	RETURN_MSTR(retval);
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
		GET_FUNC_ARG(arg1, input);

	/*
	 * Return window refnum by channel
	 */
	if (arg1 && is_channel(arg1))
	{
		int 	servnum;
		char 	*chan;
		int	win = -1;

		chan = arg1;
		if (is_string_empty(input))
			servnum = from_server;
		else
			servnum = str_to_servref(input);

		/* Now return window for *any* channel. */
		if ((win = get_channel_window(chan, servnum)))
			RETURN_INT(win);

		RETURN_INT(-1);
	}

	/*
	 * Return channel by window refnum/desc
	 */
	else 
	{
		int	win;

		if (arg1 && *arg1)
			win = lookup_window(arg1);
		else
			win = get_window_refnum(0);

		if (win > 0)
			RETURN_STR(get_window_echannel(win));
		RETURN_EMPTY;
	}
}

BUILT_IN_FUNCTION(function_findw, input)
{
	char	*word, *this_word;
	int	word_cnt = 0;

	GET_FUNC_ARG(word, input);

	while (input && *input)
	{
		GET_FUNC_ARG(this_word, input);
		if (!my_stricmp(this_word, word))
			RETURN_INT(word_cnt);

		word_cnt++;
	}

	RETURN_INT(-1);
}

BUILT_IN_FUNCTION(function_findws, input)
{
	char	*word, *this_word;
	int	word_cnt;
	char	*ret = NULL;
	size_t	clue = 0;

	GET_FUNC_ARG(word, input);

	for (word_cnt = 0; input && *input; word_cnt++)
	{
		GET_FUNC_ARG(this_word, input);
		if (!my_stricmp(this_word, word))
			malloc_strcat_word_c(&ret, space, ltoa(word_cnt), DWORD_NO, &clue);
	}

	RETURN_MSTR(ret);
}


BUILT_IN_FUNCTION(function_uhc, input)
{
	char 	*nick, *user, *host;

	if (!input || !*input)
		RETURN_EMPTY;

	if (figure_out_address(input, &nick, &user, &host))
		RETURN_STR(input);
	else
		return malloc_sprintf(NULL, "%s!%s@%s", 
				*nick ? nick : star,
				*user ? user : star,
				*host ? host : star);
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


BUILT_IN_FUNCTION(function_ftime, words)
{
	char *	filename;
	Filename fullname;
	Stat 	s;

	GET_DWORD_ARG(filename, words);

	if (!filename || !*filename)
		filename = words, words = NULL;

	if (normalize_filename(filename, fullname))
		RETURN_EMPTY;

	if (stat(fullname, &s) == -1)
		RETURN_EMPTY;

	RETURN_INT(s.st_mtime);
}

BUILT_IN_FUNCTION(function_irclib, input)
{
	RETURN_STR(irc_lib);
}

BUILT_IN_FUNCTION(function_substr, input)
{
	char *srch;
	ssize_t span;

	GET_DWORD_ARG(srch, input);

	if ((span = stristr(input, srch)) >= 0)
		RETURN_INT((unsigned long) span);
	else
		RETURN_INT(-1);
}

BUILT_IN_FUNCTION(function_rsubstr, input)
{
	char *srch;
	ssize_t	span;

	GET_DWORD_ARG(srch, input);

	if ((span = rstristr(input, srch)) >= 0)
		RETURN_INT((unsigned long) span);
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
			case COLOR_TAG:
			case COLOR256_TAG:
			case '\033':	/* \033 is ESCape */
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

	RETURN_INT(is_server_registered(refnum));
}

BUILT_IN_FUNCTION(function_currchans, input)
{
	int server = -1;
	int	win = 0;
	char *retval = NULL;
	const char *chan;
	size_t	clue;

	if (input && *input)
		GET_INT_ARG(server, input)
	else
		server = -2;		/* DONT CHANGE THIS TO NOSERV */

	if (server == -1)
		server = from_server;

	clue = 0;
	while (traverse_all_windows2(&win))
	{
		if (server != -2 && get_window_server(win) != server)
			continue;
		if (!(chan = get_window_echannel(win)))
			continue;

		malloc_strcat_wordlist_c(&retval, space, "\"", &clue);
		malloc_strcat_wordlist_c(&retval, empty_string, ltoa(get_window_server(win)), &clue);
		malloc_strcat_wordlist_c(&retval, space, chan, &clue);
		malloc_strcat_wordlist_c(&retval, empty_string, "\"", &clue);
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

	GET_FUNC_ARG(fname, input);
	RETURN_INT(func_exist(fname));
}

BUILT_IN_FUNCTION(function_cexist, input)
{
	char *fname;

	GET_FUNC_ARG(fname, input);
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

BUILT_IN_FUNCTION(function_regcomp_cs, input)
{
	char *	dest;
	regex_t preg;

	memset(&preg, 0, sizeof(preg)); 	/* make valgrind happy */
	last_regex_error = regcomp(&preg, input, REG_EXTENDED);

	dest = transform_string_dyn("+ENC", (char *)&preg, 
					sizeof(regex_t), NULL);
	RETURN_MSTR(dest);
}

BUILT_IN_FUNCTION(function_regcomp, input)
{
	char *	dest;
	regex_t preg;

	memset(&preg, 0, sizeof(preg)); 	/* make valgrind happy */
	last_regex_error = regcomp(&preg, input, REG_EXTENDED | REG_ICASE);

	dest = transform_string_dyn("+ENC", (char *)&preg, 
					sizeof(regex_t), NULL);
	RETURN_MSTR(dest);
}

BUILT_IN_FUNCTION(function_regexec, input)
{
	char *	unsaved;
	regex_t preg;

	GET_FUNC_ARG(unsaved, input);
	if (strlen(unsaved) != sizeof(regex_t) * 2)
	{
		yell("First argument to $regexec() isn't proper length");
		RETURN_EMPTY;
	}

	/* XXX - What if this fails? */
	transform_string(ENC_xform, XFORM_DECODE, NULL,
			 unsaved, strlen(unsaved),
			 (char *)&preg, sizeof(preg));
	last_regex_error = regexec(&preg, input, 0, NULL, 0);
	RETURN_INT(last_regex_error);	/* DONT PASS FUNC CALL TO RETURN_INT */
}

BUILT_IN_FUNCTION(function_regmatches, input)
{
	char *	unsaved;
	size_t	nmatch;
	char *	ret = NULL;
	regex_t preg;
	regmatch_t *pmatch = NULL;

	GET_FUNC_ARG(unsaved, input);
	GET_INT_ARG(nmatch, input);

	if (strlen(unsaved) != sizeof(regex_t) * 2)
	{
		yell("First argument to $regmatches() isn't proper length");
		RETURN_EMPTY;
	}

	/* XXX - What if this fails? */
	transform_string(ENC_xform, XFORM_DECODE, NULL,
			 unsaved, strlen(unsaved),
			 (char *)&preg, sizeof(preg));

	RESIZE(pmatch, regmatch_t, nmatch);
	if (!(last_regex_error = regexec(&preg, input, nmatch, pmatch, 0)))
	{
	    size_t	n, clue = 0;
	    const unsigned char *rm_so_str, *rm_eo_str;
	    int		rm_so_str_cnt, rm_eo_str_cnt;

	    for (n = 0; n < nmatch; n++) 
	    {
		if (pmatch[n].rm_so < 0)
		{
			/* This may need to be continue depending
			 * on the regex implementation */
			break;
		}
		rm_so_str = input + pmatch[n].rm_so;
		rm_eo_str = input + pmatch[n].rm_eo;
		rm_so_str_cnt = count_initial_codepoints(input, rm_so_str);
		rm_eo_str_cnt = count_initial_codepoints(input, rm_eo_str);

		malloc_strcat_word_c(&ret, space, 
					ltoa(rm_so_str_cnt),
					DWORD_NO, &clue);
		malloc_strcat_word_c(&ret, space, 
					ltoa(rm_eo_str_cnt - rm_so_str_cnt),
					DWORD_NO, &clue);
	    }
	}
	new_free((char **)&pmatch);
	RETURN_MSTR(ret);	/* DONT PASS FUNC CALL TO RETURN_INT */
}

BUILT_IN_FUNCTION(function_regerror, input)
{
	char *	unsaved;
	char	error_buf[1024];
	regex_t	preg;

	GET_FUNC_ARG(unsaved, input);
	if (strlen(unsaved) != sizeof(regex_t) * 2)
	{
		yell("First argument to $regmatches() isn't proper length");
		RETURN_EMPTY;
	}

	/* XXX - What if this fails? */
	transform_string(ENC_xform, XFORM_DECODE, NULL,
			 unsaved, strlen(unsaved),
			 (char *)&preg, sizeof(preg));

	*error_buf = 0;
	if (last_regex_error)
		regerror(last_regex_error, &preg, error_buf, sizeof(error_buf));
	RETURN_STR(error_buf);
}

BUILT_IN_FUNCTION(function_regfree, input)
{
	char *unsaved;
	regex_t	preg;

	GET_FUNC_ARG(unsaved, input);
	if (strlen(unsaved) != sizeof(regex_t) * 2)
	{
		yell("First argument to $regmatches() isn't proper length");
		RETURN_EMPTY;
	}

	/* XXX - What if this fails? */
	transform_string(ENC_xform, XFORM_DECODE, NULL,
			 unsaved, strlen(unsaved),
			 (char *)&preg, sizeof(preg));

	regfree(&preg);
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

	GET_FUNC_ARG(env, input);
	RETURN_STR(getenv(env));
}

BUILT_IN_FUNCTION(function_count, input)
{
	char 	*str;
	const char *str2;
	int 	count = 0;
	ssize_t	span;

	GET_DWORD_ARG(str, input);
	str2 = input;

	for (;;)
	{
		if ((span = stristr(str2, str)) < 0)
			break;

		count++;
		str2 += span + 1;
	}

	RETURN_INT(count);
}

/*
 * Usage: $randread(filename)
 * Returns: A psuedo-random line in the specified file
 * Based on work contributed by srfrog
 *
 * XXX This function is not encoding aware.
 * I'm not even sure if that's possible.
 */
BUILT_IN_FUNCTION(function_randread, input)
{
	FILE 	*fp;
	char 	buffer[BIG_BUFFER_SIZE + 1];
	char 	*filename;
	Filename fullname;
	off_t	filesize, offset;

	*buffer = 0;
	GET_DWORD_ARG(filename, input);

	if (normalize_filename(filename, fullname))
		RETURN_EMPTY;
	if ((filesize = file_size(fullname)) <= 0)
		RETURN_EMPTY;
	if ((fp = fopen(fullname, "r")) == NULL)
		RETURN_EMPTY;

	offset = random_number(0) % filesize - 1;
	fseek(fp, offset, SEEK_SET);
	if (!fgets(buffer, BIG_BUFFER_SIZE, fp))
		(void) 0;
	if (!fgets(buffer, BIG_BUFFER_SIZE, fp))
		(void) 0;
	if (feof(fp))
	{
		fseek(fp, 0, SEEK_SET);
		if (!fgets(buffer, BIG_BUFFER_SIZE, fp))
			yell("I'm having some difficulty in $randread().");
	}
	chomp(buffer);
	fclose(fp);

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
 * Rewritten mostly from scratch, on Feb 14, 2005
 */
BUILT_IN_FUNCTION(function_msar, input)
{
	int	variable = 0, 
		global = 0, 
		case_sensitive = 0;
	int	delimiter;
	char *	last_segment;
	char *	text;
	char *	after;
	char *	workbuf = NULL;
	char *	search;
	char *	replace;
	size_t	cpoffset;
	char	*s, *p;
	unsigned char	delimstr[16];

	/*
	 * Scan the leading part of the argument list, slurping up any
	 * options, and grabbing the delimiter character.  If we don't
	 * come across a delimiter, then just end abruptly.
	 */
	for (;; input++)
	{
		if (*input == 'r')
			variable = 1;
		else if (*input == 'g')
			global = 1;
		else if (*input == 'i')
			case_sensitive = 0;
		else if (*input == 'c')
			case_sensitive = 1;
		else if (!*input)
			RETURN_EMPTY;
		else
		{
			while ((delimiter = next_code_point(CUC_PP &input, 0)) == -1)
				input++;
			break;
		}
	}

	ucs_to_utf8(delimiter, delimstr, sizeof(delimstr));

	/* Now that we have the delimiter, find out what we're substituting */
	if (!(last_segment = (char *)(intptr_t)rcpindex(input + strlen(input), input, delimstr, 1, &cpoffset)))
		RETURN_EMPTY;

	/* 
	 * The last segment is either a text string, or a variable.  If it
	 * is a variable, look up its value.
	 *
	 * XXX This two-step is required because the delimiter might be
	 * more than one byte.  So we null out the first byte, but move
	 * last segment to the first byte of the following CP.
	 */
	p = s = last_segment;
	next_code_point(CUC_PP &s, 1);
	*p = 0;
	last_segment = s;

	if (variable == 1) 
		text = get_variable(last_segment);
	else
		text = malloc_strdup(last_segment);

	/*
	 * Perform substitutions while there are still pairs remaining.
	 */
	while (input && *input)
	{
	    /*
	     * "input" contains pairs of strings like:
	     *	search<delim>replace[<delim>|<eol>]
	     *
	     * If we don't find a pair, then perform no substitution --
	     * just bail out right here.
	     */
	    search = next_in_div_list(input, &after, delimiter);
	    if (!search || !*search)
		break;
	    replace = next_in_div_list(after, &after, delimiter);
/*
	    if (!replace || !*replace)
		break;
*/
	    input = after;

	    workbuf = substitute_string(text, search, replace, 
					case_sensitive, global);
	    new_free(&text);
	    text = workbuf;
	}

        if (variable) 
                add_var_alias(last_segment, text, 0);
	return text;
}

/*
 * Usage: $leftpc(COUNT TEXT)
 * Return the *longest* initial part of TEXT that includes COUNT + 1 printable
 * characters. (Don't ask)
 */
BUILT_IN_FUNCTION(function_leftpc, word)
{
	unsigned char ** 	prepared = NULL;
	char *		retval;
	int		my_lines = 1;
	int		count;

	GET_INT_ARG(count, word);
	if (count < 0 || !*word)
		RETURN_EMPTY;

	/* Convert the string to "attribute format" */
	word = new_normalize_string(word, 0, NORMALIZE);

	/* Count off the first line of stuff */
	prepared = prepare_display(-1, word, count, &my_lines, PREPARE_NOWRAP);

	/* Convert the first line back to "logical format" */
	retval = denormalize_string(prepared[0]);

	/* Clean up and return. */
	new_free(&word);
	return retval;
}


/*
 * This was written to have the same net-effect as bitchx.  However, we use
 * ^C codes rather than ansi codes as bitchx does.  You shouldnt notice one
 * way or the other.
 */
char *	function_cparse (char *input)
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
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '0';
				break;
			case 'r':
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '1';
				break;
			case 'g':
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '2';
				break;
			case 'y':
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '3';
				break;
			case 'b':
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '4';
				break;
			case 'm':
			case 'p':
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '5';
				break;
			case 'c':
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '6';
				break;
			case 'w':
				output[j++] = COLOR_TAG;
				output[j++] = '3';
				output[j++] = '7';
				break;

			case 'K':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '0';
				break;
			case 'R':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '1';
				break;
			case 'G':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '2';
				break;
			case 'Y':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '3';
				break;
			case 'B':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '4';
				break;
			case 'M':
			case 'P':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '5';
				break;
			case 'C':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '6';
				break;
			case 'W':
				output[j++] = COLOR_TAG;
				output[j++] = '5';
				output[j++] = '7';
				break;

			case '0': case '1': case '2': case '3': 
			case '4': case '5': case '6': case '7':
				output[j++] = COLOR_TAG;
				output[j++] = ',';
				output[j++] = '4';
				output[j++] = stuff[i];
				break;

			case 'F':
				output[j++] = BLINK_TOG;
				break;
			case 'n':
				output[j++] = COLOR_TAG;
				output[j++] = '-';
				output[j++] = '1';
				output[j++] = ALL_OFF;
				break;
			case 'X':
				output[j++] = COLOR256_TAG;
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
		output[j++] = COLOR_TAG;
		output[j++] = '-';
		output[j++] = '1';
	}
	output[j] = 0;

	if (expand)
	{
		stuff = expand_alias(output, input);
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
		input = LOCAL_COPY("%s %r");

	for (i = 0, len = strlen(input); i < len; i++)
	{
		if (ptr - tmp >= (int)size)
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
	int	w = 0;
	char *	nick = NULL;
	int	servref = -1;
	const char *q;

	GET_FUNC_ARG(nick, args);
	if (args && *args)
		GET_INT_ARG(servref, args);

	while (traverse_all_windows2(&w))
	{
	    q = get_window_equery(w);
	    if (q && !my_stricmp(q, nick))
		if (servref < 0 || servref == get_window_server(w))
			RETURN_INT(get_window_user_refnum(w));
	}

	RETURN_INT(-1);
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
 * 10:*!*user@* 
 */
BUILT_IN_FUNCTION(function_mask, args)
{
	char **my_dot = NULL, **colon = NULL;
 	char *buff, *my_nickname, *my_username, *my_hostname, *dmask, *dbuff, *p, *fa;
	unsigned i, coloncount, dotcount, nondigit, method, userlen;
	int	size;

	fa = new_next_arg(args, &args);

	if (is_number(fa)) {
		method = my_atol(fa);
		my_nickname = args;
	} else {
		my_nickname = fa;
		GET_INT_ARG(method, args);
	}

	if ((p = strchr(my_nickname, '!'))) {
		*p++ = '\0';
		my_username = p;
	} else {
		p = my_nickname;
		my_nickname = LOCAL_COPY("*");
		my_username = p;
	}

	if ((p = strchr(p, '@'))) {
		*p++ = '\0';
		my_hostname = p;
		userlen = strlen(my_username);
	} else {
		RETURN_EMPTY;
	}

	buff = (char *) new_malloc(BIG_BUFFER_SIZE + 1);
	size = strlen(my_hostname) + 2;
	dbuff = (char *) new_malloc(size);
	p = dmask = dbuff + 1;
	strlcpy(p, my_hostname, size);

	for (i = coloncount = dotcount = nondigit = 0; p[i]; i++) {
		if (p[i] == '.') {
			if (!(dotcount % 50)) {
				new_realloc((void **) &my_dot,
					sizeof(char *) * (dotcount + 50));
			}

			my_dot[dotcount++] = p + i;
		} else if (p[i] == ':') {
			if (!(coloncount % 50)) {
				new_realloc((void **) &colon,
					sizeof(char *) * (coloncount + 50));
			}

			colon[coloncount++] = p + i;
		} else if (!isdigit(p[i])) {
			nondigit++;
		}
	}

	if (coloncount >= 2) {
		*(colon[1] + 1) = '*';
		*(colon[1] + 2) = '\0';
	} else if (!nondigit && dotcount == 3) {
		*(my_dot[2] + 1) = '*';
		*(my_dot[2] + 2) = '\0';
	} else if (dotcount >=2) {
		/* Treat domains like '.co.uk' as a tld. */

		if ((p + i - my_dot[dotcount - 2]) < 7) {
			if (dotcount >= 3) {
				dmask = my_dot[dotcount - 3] - 1;
				*dmask = '*';
			}
		} else {
			dmask = my_dot[dotcount - 2] - 1;
			*dmask = '*';
		}
	}

	switch (method) {
		case 0:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "*!%s@%s", my_username,
				my_hostname);
			break;
		case 1:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "*!*%s@%s",
				userlen > 7 ? my_username + 1 : my_username,
				my_hostname);
			break;
		case 2:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "*!*@%s",
				my_hostname);
			break;
		case 3:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "*!*%s@%s",
				userlen > 7 ? my_username + 1 : my_username, dmask);
			break;
		case 4:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "*!*@%s", dmask);
			break;
		case 5:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "%s!%s@%s",
				my_nickname, my_username, my_hostname);
			break;
		case 6:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "%s!*%s@%s",
				my_nickname, userlen > 7 ? my_username + 1 : my_username,
				my_hostname);
			break;
		case 7:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "%s!*@%s", my_nickname,
				my_hostname);
			break;
		case 8:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "%s!*%s@%s",
				my_nickname, userlen > 7 ? my_username + 1 : my_username,
				dmask);
			break;
		case 9:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "%s!*@%s", my_nickname,
				dmask);
			break;
		case 10:
			snprintf(buff, BIG_BUFFER_SIZE + 1, "*!*%s@*", my_username);
			break;
		default:
			new_free(&dbuff);
			new_free(&my_dot);
			new_free(&colon);

			RETURN_EMPTY;
	}

	new_free(&dbuff);
	new_free(&my_dot);
	new_free(&colon);

	RETURN_STR(buff);
}

BUILT_IN_FUNCTION(function_ischanvoice, input)
{
	char	*nick;

	GET_FUNC_ARG(nick, input);
	RETURN_INT(is_chanvoice(input, nick));
}

BUILT_IN_FUNCTION(function_ishalfop, input)
{
	char	*nick;

	GET_FUNC_ARG(nick, input);
	RETURN_INT(is_halfop(input, nick));
}

BUILT_IN_FUNCTION(function_servports, input)
{
	int	servnum = from_server;

	if (*input)
		GET_INT_ARG(servnum, input);

	if (servnum == -1)
		servnum = from_server;
	if (servnum < 0 || servnum > server_list_size())
		RETURN_EMPTY;

	return malloc_sprintf(NULL, "%d %d", get_server_port(servnum),
				  get_server_local_port(servnum));
}

BUILT_IN_FUNCTION(function_chop, input)
{
	char *buffer;
	int howmany = 1;

	if (my_atol(input))
		GET_INT_ARG(howmany, input);

	buffer = malloc_strdup(input);
	chop(buffer, howmany);
	return buffer;
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
	int	window = 0;
const 	char *	chan;
	char *	arg;

	GET_FUNC_ARG(arg, input);
	while (traverse_all_windows2(&window))
	{
		/*
		 * Check to see if the channel specified is the current
		 * channel on *any* window for the current server.
		 */
		if ((chan = get_window_echannel(window)) &&
				!my_stricmp(arg, chan) && 
				get_window_server(window) == from_server)
			RETURN_INT(1);
	}

	RETURN_INT(0);
}

/* <horny> can you add something for me?  */
BUILT_IN_FUNCTION(function_channel, input)
{
	char *chan;

	chan = next_func_arg(input, &input);	/* dont use GET_FUNC_ARG */
	return scan_channel(chan);
}

BUILT_IN_FUNCTION(function_pad, input)
{
	int	width;
	size_t	awidth, len;
	unsigned char	*pads;
	char	*retval;
	size_t	retvalsiz;
	int	codepoint;
	int	j;

	GET_INT_ARG(width, input);
	GET_DWORD_ARG(pads, input);

	if ((codepoint = next_code_point(CUC_PP &pads, 0)) == -1)
		codepoint = ' ';	/* Sigh */

        if (width < 0)
	{
		width = -width;
		j = 1;
	}
	else if (width > 0)
		j = -1;
	else
		j = 0;

	retval = fix_string_width(input, j, codepoint, width, 0);
	RETURN_MSTR(retval);
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
		*right = NULL,
		*booya = NULL;
	char	**lhs = NULL,
		**rhs = NULL;
	int	leftc,
		rightc,
		righti;
	size_t	rvclue=0;

	left = word;
	if (!(right = strchr(word,'/')))
		RETURN_EMPTY;

	*right++ = 0;
	leftc = splitw(left, &lhs, DWORD_DWORDS);
	if (leftc > 0)
		qsort((void *)lhs, leftc, sizeof(char *), sort_it);
	rightc = splitw(right, &rhs, DWORD_DWORDS);

	for (righti = 0; righti < rightc; righti++)
	{
		if (leftc <= 0 || !bsearch(&rhs[righti], lhs, leftc, sizeof(char *), sort_it))
			malloc_strcat_word_c(&booya, space, rhs[righti], DWORD_DWORDS, &rvclue);
	}

	new_free((char **)&lhs);
	new_free((char **)&rhs);

	RETURN_MSTR(booya);
}

BUILT_IN_FUNCTION(function_printlen, input)
{
	unsigned char *copy;
	int	retval;

	copy = new_normalize_string(input, 2, NORMALIZE);
	retval = output_with_count(copy, 0, 0);
	new_free(&copy);
	RETURN_INT(retval);
}

BUILT_IN_FUNCTION(function_stripansicodes, input)
{
        return new_normalize_string(input, 1, NORMALIZE);
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
	int	segments = 0;

	/*
	 * See if the first arg is the base
	 */
	barg = next_arg(input, &input);

	/*
	 * If it is, the number is the 2nd arg
	 */
	if (barg && *barg == 'b' && is_number(barg + 1))
	{
		GET_FUNC_ARG(number, input);
	}
	/*
	 * Otherwise, the first arg is the number,
	 * the 2nd arg probably is the base
	 */
	else
	{
		number = barg;
		barg = next_arg(input, &input);
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

	/* XXX Why doesn't this use strtoimax? */
	strtol(number, &endptr, base);
	/* 
	 * You don't have to remind me that endptr can't be null
	 * here.  This is just to make clang be quiet 
	 */
	if (!endptr)
		RETURN_INT(0);

	if (endptr > number)
		segments++;
	if (*endptr == '.')
	{
		if (base == 0)
			base = 10;	/* XXX So i'm chicken. */
		number = endptr + 1;
		/* XXX Why doesn't this use strtoimax? */
		strtol(number, &endptr, base);
		if (endptr > number)
			segments++;
	}

	if (*endptr || segments == 0)
		RETURN_INT(0);
	else
		RETURN_INT(1);
}

/*
 * $rest(index string)
 * 	Returns 'string' starting at the 'index'th character
 * $rest(string)
 * 	Returns 'string' without the first character.
 * Just like $restw() does for words.
 * Technically, 'index' can be negative, but this isn't documented.
 */
BUILT_IN_FUNCTION(function_rest, input)
{
	int	start = 1;
	char *	test_input;
	int	len;
	char *	x;

	/*
	 * XXX - The 'index' argument should not be optional.
	 * So we have to determine if the 1st word is a number.
	 */
	test_input = input;
	parse_number(&test_input);
	if (test_input > input && my_isspace(*test_input))
		GET_INT_ARG(start, input);

	len = quick_code_point_count(input);

	if (start >= len || -start >= len)
		RETURN_EMPTY;
	else if (start >= 0)
	{
		/* Skip 'start' code points at the start */
		x = input;
		while (start-- > 0)
			next_code_point(CUC_PP &x, 1);
		RETURN_STR(x);
	}
	else
	{
		/* Walk 'start' code points from the end */
		x = input + strlen(input);
		while (start++ < 0)
			previous_code_point(input, CUC_PP &x);
		RETURN_STR(x);
	}
}


/* Written by panasync */
/* Re-written by CE */
#define GET_UNIFIED_ARRAY_FUNCTION(thisfn, nextfn)	\
BUILT_IN_FUNCTION( thisfn , input)			\
{							\
	char *s = NULL;					\
	char *r;					\
	char *ret = NULL;				\
	char **subresults;			\
	size_t	clue = 0;				\
	int	howmany = 0;				\
							\
	if (!input || !*input)					\
	{							\
		subresults = nextfn ("*", &howmany, 0, 0, 0);	\
		ret = unsplitw(&subresults, howmany, DWORD_DWORDS);	\
		RETURN_MSTR(ret);				\
	}							\
								\
	while ((s = next_arg(input, &input)))			\
	{							\
		subresults = nextfn (s, &howmany, 0, 0, 0);		\
		r = unsplitw(&subresults, howmany, DWORD_DWORDS);	\
		malloc_strcat_wordlist_c(&ret, space, r, &clue);	\
		new_free(&r);					\
	}							\
								\
	RETURN_MSTR(ret);					\
}

GET_UNIFIED_ARRAY_FUNCTION(function_getsets, pmatch_builtin_variables)
GET_UNIFIED_ARRAY_FUNCTION(function_getcommands, pmatch_builtin_commands)
GET_UNIFIED_ARRAY_FUNCTION(function_getfunctions, pmatch_builtin_functions)

/*
 * XXX This should be an alias.
 */
BUILT_IN_FUNCTION(function_stripc, input)
{
	char	*output;

	output = new_normalize_string(input, 1, STRIP_COLOR);
	RETURN_MSTR(output);
}

BUILT_IN_FUNCTION(function_stripcrap, input)
{
	char	*how;
	int	mangle;
	char	*output;

	GET_FUNC_ARG(how, input);
	mangle = parse_mangle(how, 0, NULL);

	output = new_normalize_string(input, 1, mangle);
	RETURN_MSTR(output);
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
static	size_t	input_size = 0;
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
	GET_FUNC_ARG(optopt_var, input); 
	GET_FUNC_ARG(optarg_var, input); 
	GET_DWORD_ARG(optstr, input);
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
					strlcat(args, tmp, input_size + 1);
					strlcat(args, " ", input_size + 1);
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
			strlcat(extra_args, tmp, input_size * 2 + 1);
			strlcat(extra_args, input, input_size * 2 + 1);
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
			strlcat(args, tmp, input_size + 1);
			strlcat(args, " ", input_size + 1);
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
			strlcat(extra_args, tmp, input_size * 2 + 1);
			strlcat(extra_args, " ", input_size * 2 + 1);
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
	    strlcat(args, extra_args, input_size);

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
			/* add_var_alias(optarg_var, args, 0); */
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
		GET_FUNC_ARG(arg, input)

		if ((len = quick_code_point_count(arg)) > maxlen)
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

	numwords = splitw(input, &words, DWORD_DWORDS);
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
			strlcpy(retval, words[0], len_index);
			new_free((char **)&words);
			return retval;
		}
	    }
	}

	malloc_strcat_word(&retval, space, words[0], DWORD_DWORDS);
	new_free((char **)&words);
	RETURN_MSTR(retval);
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

	word = new_next_arg(input, &input);
	len = my_atol(safe_new_next_arg(input, &input));
	if (!word || !*word)
		word = input;

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
 *
 * 10/01/02 At the suggestion of fudd and rain, if pos == len, then return
 * the number of words in 'input' because if the cursor is at the end of the
 * input prompt and you do $indextoword($curpos() $L), right now it would
 * return EMPTY but it should return the word number right before the cursor.
 */
BUILT_IN_FUNCTION(function_indextoword, input)
{
	unsigned char *s;
	int	pos, count;
	int	i;

	GET_INT_ARG(pos, input);
	count = quick_code_point_count(input);
	if (pos < 0 || pos > count)
		RETURN_EMPTY;

	s = input;
	for (i = 0; i < pos; i++)
		next_code_point(CUC_PP &s, 1);

	/*
	 * This is a special case to handle multiple runs of words.
	 * According to our wacky word rules, the *FIRST* space after
	 * a word belongs to the previous word; but the second and
	 * subsequent spaces belong to the following word. (urgh).
	 *
	 * In order to "fake" this, we put a non-space character right
	 * here to make it "count" as a word if it isn't the first one.
	 * But naturally, we only do that if we're not at the eos.
	 */
	if (*s)
	{
		*s = 'x';
		*++s = 0;
	}

	RETURN_INT(count_words(input, DWORD_DWORDS, "\"") - 1);
}

BUILT_IN_FUNCTION(function_realpath, input)
{
	Filename	resolvedname;

	if (!normalize_filename(input, resolvedname))
		RETURN_STR(resolvedname);

	RETURN_EMPTY;
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
BUILT_IN_FUNCTION(function_insert, input)
{
	unsigned char *s, *p;
	char 	*first_part, *second_part, *extra;
	int	where;
	char	*retval;

	GET_INT_ARG(where, input);
	GET_DWORD_ARG(extra, input);

	s = first_part = input;
	for (;;)
	{
		if (where <= 0)
			break;
		if (!*s)
			break;

		while (next_code_point(CUC_PP &s, 0) == -1)
			s++;
		where--;
	}

	second_part = LOCAL_COPY(s);
	*s = 0;

	retval = malloc_strdup3(first_part, extra, second_part);
	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_stat, words)
{
	Filename expanded;
	char *	filename;
	char 	retval[BIG_BUFFER_SIZE];
	Stat	sb;


	if (!(filename = new_next_arg(words, &words)))
		filename = words;

	if (!filename || !*filename)
		RETURN_INT(-1);

	if (normalize_filename(filename, expanded))
		RETURN_INT(-1);

	if (stat(expanded, &sb) < 0)
		RETURN_EMPTY;

	snprintf(retval, BIG_BUFFER_SIZE,
		"%d %d %o %d %d %d %d "
		UINTMAX_FORMAT" "UINTMAX_FORMAT" "UINTMAX_FORMAT" "
		INTMAX_FORMAT" "INTMAX_FORMAT" "INTMAX_FORMAT,
		(int)	sb.st_dev,		/* device number */
		(int)	sb.st_ino,		/* Inode number */
		(int)	sb.st_mode,		/* Permissions */
		(int)	sb.st_nlink,		/* Hard links */
		(int)	sb.st_uid,		/* Owner UID */
		(int)	sb.st_gid,		/* Owner GID */
		(int)	sb.st_rdev,		/* Device type */
		(uintmax_t)sb.st_size,		/* Size of file */
		(uintmax_t)sb.st_blksize,	/* Size of each block */
		(uintmax_t)sb.st_blocks,	/* Blocks used in file */
		(intmax_t)sb.st_atime,		/* Last-access time */
		(intmax_t)sb.st_mtime,		/* Last-modified time */
		(intmax_t)sb.st_ctime		/* Last-change time */
			);

	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_isdisplaying, input)
{
	RETURN_INT(get_window_display());
}

BUILT_IN_FUNCTION(function_getcap, input)
{
	char *	type;

	GET_FUNC_ARG(type, input);
	if (!my_stricmp(type, "TERM"))
	{
		const char *	retval;
		char *	term = NULL;
		int	querytype = 0;
		int	mangle = 1;
		
		GET_FUNC_ARG(term, input);
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
	char *(*efunc) (void) = NULL;
	IrcVariable *sfunc;

	get_var_alias(input, &efunc, &sfunc);
	if (efunc == NULL)
		RETURN_EMPTY;
	return efunc();
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
	int	window = 0;

	while (traverse_all_windows2(&window))
		if (get_window_notified(window))
			malloc_strcat_word_c(&retval, space, ltoa(get_window_user_refnum(window)), DWORD_NO, &rvclue);

	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_loadinfo, input)
{
	return malloc_sprintf(NULL, "%d %s %s %s" , current_line(), current_filename()
			, current_loader(), current_package() ? current_package() : empty_string);
}

BUILT_IN_FUNCTION(function_wordtoindex, input)
{
	int		wordnum;
	const char *	ptr;
	int		cpidx;

	GET_INT_ARG(wordnum, input);
	real_move_to_abs_word(input, &ptr, wordnum, DWORD_DWORDS, "\"");
	cpidx = quick_code_point_index((const unsigned char *)input, (const unsigned char *)ptr);
	RETURN_INT(cpidx);
}

/*
 * These four functions contributed by 
 * B. Thomas Frazier (tfrazier@mjolnir.gisystems.net)
 * On December 12, 2000.
 */

BUILT_IN_FUNCTION(function_iptolong, word)
{
	ISA	addr;
	char *	dotted_quad;

	addr.sin_family = AF_INET;
	GET_FUNC_ARG(dotted_quad, word);
	if (inet_strton(dotted_quad, NULL, (SA *)&addr, AI_NUMERICHOST))
		RETURN_EMPTY;
	
	return malloc_sprintf(NULL, UINTMAX_FORMAT, 
				(uintmax_t)ntohl(addr.sin_addr.s_addr));
}

BUILT_IN_FUNCTION(function_longtoip, word)
{
	char *	ip32;
	SS	addr;
	char	retval[256];

	GET_FUNC_ARG(ip32, word);
	((SA *)&addr)->sa_family = AF_INET;
	if (inet_strton(ip32, NULL, (SA *)&addr, AI_NUMERICHOST))
		RETURN_EMPTY;
	inet_ntostr((SA *)&addr, retval, 256, NULL, 0, NI_NUMERICHOST);
	RETURN_STR(retval);
}

BUILT_IN_FUNCTION(function_isencrypted, input)
{
	int	sval = from_server;

	if (*input)
		GET_INT_ARG(sval, input);

	/* garbage in, garbage out. */
	if (sval < 0 || sval >= server_list_size())
		RETURN_INT(0);

	/* Check if it is encrypted connection */
	RETURN_INT(get_server_ssl_enabled(sval));
}

BUILT_IN_FUNCTION(function_ssl, words)
{
	RETURN_INT(client_ssl_enabled());
}

BUILT_IN_FUNCTION(function_cipher, input)
{
	int     	sval = from_server;
	const char *	ret;

	if (*input)
		GET_INT_ARG(sval, input);

	if (sval < 0 || sval >= server_list_size())
		RETURN_EMPTY;

	ret = get_server_ssl_cipher(sval);
	RETURN_STR(ret);
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


MATH_FUNCTION(function_abs, fabs)
MATH_FUNCTION(function_ceil, ceil)
MATH_FUNCTION(function_floor, floor)

MATH_FUNCTION(function_exp, exp)
MATH_FUNCTION(function_log, log)
MATH_FUNCTION(function_log10, log10)

MATH_FUNCTION(function_cosh, cosh)
MATH_FUNCTION(function_sinh, sinh)
MATH_FUNCTION(function_tanh, tanh)
MATH_FUNCTION(function_acosh, acosh)
MATH_FUNCTION(function_asinh, asinh)
MATH_FUNCTION(function_atanh, atanh)

MATH_FUNCTION(function_cos, cos)
MATH_FUNCTION(function_sin, sin)
MATH_FUNCTION(function_tan, tan)
MATH_FUNCTION(function_acos, acos)
MATH_FUNCTION(function_asin, asin)

MATH_FUNCTION2(function_jn, jn)
MATH_FUNCTION2(function_yn, yn)

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

#ifdef HAVE_PERL

BUILT_IN_FUNCTION(function_perl, input)
{
	return perleval ( input );
}

BUILT_IN_FUNCTION(function_perlcall, input)
{
	char *sub=NULL;

	GET_DWORD_ARG(sub, input);
	return perlcall ( sub, NULL, NULL, -1, input );
}

BUILT_IN_FUNCTION(function_perlxcall, input)
{
	long item=0;
	char *sub=NULL, *in=NULL, *out=NULL;

	GET_DWORD_ARG(sub, input);
	if (input && *input) GET_DWORD_ARG(in, input);
	if (input && *input) GET_DWORD_ARG(out, input);
	if (input && *input) GET_INT_ARG(item, input);
	return perlcall ( sub, in, out, item, input );
}

#endif

BUILT_IN_FUNCTION(function_unsplit, input)
{
	char *	sep;
	char *	word;
	char *	retval = NULL;
	size_t	clue = 0;

	GET_DWORD_ARG(sep, input);
	while ((word = new_next_arg(input, &input)))
		malloc_strcat_word_c(&retval, sep, word, DWORD_NO, &clue);
	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_encryptparm, input)
{
	char	*ret = NULL, *entry = NULL;
	size_t	clue = 0;
	Crypt	*key;

	GET_FUNC_ARG(entry, input);
	if ((key = is_crypted(entry, from_server, NULL))) 
	{
		malloc_strcat_word_c(&ret, space, key->nick, DWORD_DWORDS, &clue);
		malloc_strcat_word_c(&ret, space, key->passwd, DWORD_DWORDS, &clue);
		malloc_strcat_word_c(&ret, space, key->prog, DWORD_DWORDS, &clue);
	}

	RETURN_MSTR(ret);
}

BUILT_IN_FUNCTION(function_serverctl, input)
{
	return serverctl(input);
}


#ifdef NO_JOB_CONTROL
BUILT_IN_FUNCTION(function_killpid, input)
{
	RETURN_EMPTY;
}
#endif

BUILT_IN_FUNCTION(function_killpid, input)
{
	char *	pid_str;
	char *	sig_str;
	pid_t	pid;
	int	sig;
	int	retval = 0;

	GET_FUNC_ARG(sig_str, input);
	if (is_number(sig_str))
	{
		sig = my_atol(sig_str);
		if ((sig < 1) || (sig >= NSIG))
			RETURN_EMPTY;
	}
	else
	{
		for (sig = 1; sig < NSIG; sig++)
		{
			if (!get_signal_name(sig))
				continue;
			if (!my_stricmp(get_signal_name(sig), sig_str))
				goto do_kill;	/* Oh, bite me. */
		}

		RETURN_EMPTY;
	}

do_kill:
	while ((pid_str = next_func_arg(input, &input)))
	{
		pid = strtoul(pid_str, &pid_str, 10);
		if (kill(pid, sig) == 0)
			retval++;
	}

	RETURN_INT(retval);
}

BUILT_IN_FUNCTION(function_bindctl, input)
{
	return bindctl(input);
}

BUILT_IN_FUNCTION(function_logctl, input)
{
	return logctl(input);
}

/*
 * Joins the word lists in two different variables together with an
 * optional seperator string.
 * Usage:  $joinstr(seperator var1 var2)
 */
BUILT_IN_FUNCTION(function_joinstr, input)
{
	char	*sep, *word;
	char	*retval = NULL, *sub = NULL;
	char	**freeit = NULL, **vals = NULL;
	size_t	valc = 0;
	size_t	retclue = 0;
	size_t	foo;

	GET_FUNC_ARG(sep, input)
	for (valc = 0; input && *input; valc++) {
		char *var;

		RESIZE(vals, vals, valc + 1);
		RESIZE(freeit, freeit, valc + 1);

		GET_FUNC_ARG(var, input)
		freeit[valc] = vals[valc] = get_variable(var);
	}

	for (;;) {
		size_t clue = 0;
		char   more = 0;

		for (foo = 0; foo < valc; foo++) {
			more |= *vals[foo];
			word = next_func_arg(vals[foo], &vals[foo]);
			if (word == NULL)
				word = endstr(vals[foo]);
			malloc_strcat2_c(&sub, foo?sep:empty_string, word, &clue);
		}

		if (!more)
			break;

		malloc_strcat_word_c(&retval, space, sub, DWORD_DWORDS, &retclue);
		*sub = 0;	/* Improve malloc performance */
	}

	for (foo = 0; foo < valc; foo++)
		new_free(&freeit[foo]);

	new_free(&freeit);
	new_free(&vals);
	new_free(&sub);

	RETURN_MSTR(retval);
}

BUILT_IN_FUNCTION(function_exec, input)
{
	char	*ret = NULL;
	char **args = NULL;
	int	count, *fds, foo;
	size_t	clue = 0;

	RETURN_IF_EMPTY(input);
	count = splitw(input, &args, DWORD_YES);
	RESIZE(args, void *, count+1);
	args[count] = NULL;

	if (!count || !args)
		RETURN_EMPTY;

	fds = open_exec_for_in_out_err(args[0], (char * const *)args);
	new_free(&args);

	if (fds)
		for (foo = 0; foo < 3; foo++)
			malloc_strcat_word_c(&ret, space, ltoa(fds[foo]), DWORD_NO, &clue);

	RETURN_MSTR(ret);
}

/*
 * getserial function:
 * arguments: <type> [...]
 * currently only 'HOOK' is available as a type, the arguments to the hook
 * type are a direction (+ or -) and a starting place  New types may be made
 * available later.
 */
#include "hook.h"

BUILT_IN_FUNCTION(function_getserial, input) {
    char *type, *sdir;
    int dir, from, serial;

    GET_FUNC_ARG(type, input);
    if (!type || !*type)
	RETURN_EMPTY;
    if (!my_stricmp(type, "HOOK")) {
	GET_FUNC_ARG(sdir, input);
	if (!strcmp(sdir, "+"))
	    dir = 1;
	else if (!strcmp(sdir, "-"))
	    dir = -1;
	else
	    RETURN_EMPTY;

	GET_INT_ARG(from, input);
	serial = hook_find_free_serial(dir, from, INVALID_HOOKNUM);
	RETURN_INT(serial);
    }

    RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_timerctl, input)
{
	return timerctl(input);
}

BUILT_IN_FUNCTION(function_dccctl, input)
{
	return dccctl(input);
}

BUILT_IN_FUNCTION(function_outputinfo, input)
{
	if (get_who_from())
		return malloc_sprintf(NULL, "%s %s", level_to_str(get_who_level()), get_who_from());
	else
		return malloc_strdup(level_to_str(get_who_level()));
}

BUILT_IN_FUNCTION(function_levelwindow, input)
{
	Mask	mask, window_mask;
	int	window = 0;
	int	server;
	int	i;
	char 	*rejects = NULL;

	GET_INT_ARG(server, input);
	str_to_mask(&mask, input, &rejects);	/* Errors are just ignored */
	while (traverse_all_windows2(&window))
	{
	    get_window_mask(window, &window_mask);
	    if (mask_isset(&mask, LEVEL_DCC) && mask_isset(&window_mask, LEVEL_DCC))
		RETURN_INT(get_window_user_refnum(window));

	    if (get_window_server(window) != server)
		continue;

	    for (i = 1; BIT_VALID(i); i++)
		if (mask_isset(&mask, i) && mask_isset(&window_mask, i))
			RETURN_INT(get_window_user_refnum(window));
	}
	RETURN_INT(-1);
}

BUILT_IN_FUNCTION(function_serverwin, input)
{
	int     	sval = from_server;
	int		window;

	if (*input)
		GET_INT_ARG(sval, input);

	window = get_server_current_window(sval);
	RETURN_INT(window);
}

BUILT_IN_FUNCTION(function_ignorectl, input)
{
        return ignorectl(input);
}

BUILT_IN_FUNCTION(function_metric_time, input)
{
	struct metric_time right_now;

	right_now = get_metric_time(NULL);
	return malloc_sprintf(NULL, "%ld %9.6f", right_now.mt_days, right_now.mt_mdays);
}

BUILT_IN_FUNCTION(function_windowctl, input)
{
        return windowctl(input);
}

/*
 * $numlines(<columns> <string>)
 * Returns the number of lines that <string> will occupy after final
 * display in a window with a width of <columns>, or nothing on error.
 */
BUILT_IN_FUNCTION(function_numlines, input)
{
	int cols;
	int numl = 0;
	char *strval;

	GET_INT_ARG(cols, input);
	if (cols < 1)
		RETURN_EMPTY;
	cols--;

	/* Normalize the line of output */
	strval = new_normalize_string(input, 0, NORMALIZE);
	prepare_display(-1, strval, cols, &numl, 0);
	new_free(&strval);
	RETURN_INT(numl+1);
}

/*
 * $strtol(<base> <number>)
 * Returns the decimal value of <number>, where number is a number
 * in base <base>. <base> must be higher than, or equal to 2, or
 * lower than, or equal to 36, or 0.
 * Returns empty on errors.
 * Written by howl
 */
BUILT_IN_FUNCTION(function_strtol, input)
{
	int	base;
	char *	number;
	intmax_t	retval;
	char *	after;

	if (!input || !*input) 
		RETURN_EMPTY;
	GET_INT_ARG(base, input);
	if (!input || !*input || (base != 0 && (base < 2 || base > 36)))
		RETURN_EMPTY;
	GET_FUNC_ARG(number, input);	/* Must not use GET_INT_ARG */

	retval = strtoimax(number, &after, base);
	/* Argh -- do we want to return error if invalid char found? */
	RETURN_INT(retval);
}

/*
 * $tobase(<base> <number>)
 * Returns the string value of decimal number <number>, converted to base
 * <base>. <base> must be higher than, or equal to 2, or lower than, or 
 * equal to 36.
 * Written by howl, from http://www.epicsol.org/~jnelson/base
 */
BUILT_IN_FUNCTION(function_tobase, input)
{
	int	c, base, len = 0, pos = 0, negate = 0;
	intmax_t	n, num;
	char *	string;
	char 	table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	len = pos = 0;
	
	if (!input || !*input)
		RETURN_EMPTY;
	GET_INT_ARG(base, input);

	if (!input || !*input || base < 2 || base > 36)
		RETURN_EMPTY;

	if (*input == '-')
	{
		negate = 1;
		input++;
	}
	GET_INT_ARG(num, input);
	/* The exponent routine below doesn't work for 0 */
	if (num == 0)
		RETURN_INT(num);

	while (pow(base, len) <= num)
		len++;

	if (!len)
		RETURN_EMPTY;
	
	if (negate == 1)
		len++;

	string = new_malloc(len + 1);
	string[len] = 0;
	
	if (negate)
	{
		string[0] = '-';
		pos++;
		len--;
	}

	while (len-- > 0)
	{
		n = (intmax_t)pow(base, len),
		c = floor(num / n);
		string[pos] = table[c];
		pos++;
		num -= n * c;
	}

	return string;
}

BUILT_IN_FUNCTION(function_startupfile, input)
{
	if (startup_file)
		RETURN_STR(startup_file);
	RETURN_EMPTY;
}

/*
 * $mktime(year month day hour minute second dst)
 * - Requires at least six (6) arguments
 * - Returns -1 on error
 * - Refer to the mktime(3) manpage for instructions on usage.
 * Written by howl
 */
BUILT_IN_FUNCTION(function_mktime, input)
{
	int ar[7], pos, retval;
	struct tm tmtime;
	
	for (pos = 0; pos < 7 && input && *input; pos++) 
		GET_INT_ARG(ar[pos], input);
	
	if (pos < 6)
		RETURN_INT(-1);

	tmtime.tm_year = ar[0];
	tmtime.tm_mon = ar[1];
	tmtime.tm_mday = ar[2];
	tmtime.tm_hour = ar[3];
	tmtime.tm_min = ar[4];
	tmtime.tm_sec = ar[5];
	tmtime.tm_isdst = (pos > 6) ? ar[6] : -1;
	
	retval = mktime(&tmtime);
	RETURN_INT(retval);	
}

BUILT_IN_FUNCTION(function_hookctl, input)
{
        return hookctl(input);
}

BUILT_IN_FUNCTION(function_fix_arglist, input)
{
	ArgList *l;
	char *r;
	l = parse_arglist(input);
	r = print_arglist(l);
	if (r)
	{
		destroy_arglist(&l);
		return r;
	}
	RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_symbolctl, input)
{
	return symbolctl(input);
}

BUILT_IN_FUNCTION(function_levelctl, input)
{
	return levelctl(input);
}

BUILT_IN_FUNCTION(function_dbmctl, input)
{
	return dbmctl(input);
}

/*
 * Here's the plan -- we're going to do this over again a second time.
 *
 * $xform("<transformations>" "meta" "meta" text)
 *
 * Where the <transformation>s are supported by transform_string().
 * At the time i write this, they are:
 *	URL		URL encoding		(reversable, no meta)
 *	ENC		Base16 encoding		(reversable, no meta)
 *	B64		Base64 encoding		(reversable, no meta)
 *	CTCP		CTCP encoding		(reversable, no meta)
 *	SED		Simple Encrypt Data	(reversable, requires meta)
 *	DEF		Default encryption	(reversable, requires meta)
 *
 * You can string together multiple transformations.  Any transformation
 * that requires a meta value (ie, a cipherkey) should be supplied after 
 * the transformations *in the correct order*.  After this should be the
 * plain text.  To apply a transformation, prefix its name with a plus sign
 * ("+") and to remove a transformation, prefix its name with a minus sign
 * ("-").  For example, +URL means url encode, and -URL means url decode.
 *
 * The transformations is a dword (must be surrounded by double quotes if 
 * it contains a space, which it will if you do multiple transformations).
 * The meta values are dwords (must be surrounded by double quotes if they
 * contain a space).  These two things make this function behave differently
 * than functions normally do, so this is a documented deviancy!
 *
 * Examples:
 * 	URL-encode a string	$xform(+URL this is a string)
 *	URL-decode a string	$xform(-URL this%20is%20a%20string)
 *	SED-cipher a string	$xform(+SED password this is a string)
 *	SED-decipher a string	$xform(-sed password <whatever>)
 *
 * More practical examples:
 * 1) Read binary data from a file, encrypt it, and url encode it again.
 *	@fd = open(file.txt R)
 *	@data = read($fd 1024)
 *	@cipher = xform("-CTCP +SED +URL" password $data)
 *	@close($fd)
 *	msg someone $cipher
 *
 * Why does this work?  
 *  -- $read() returns ctcp-enquoted data, so -CTCP removes it
 *  -- Now we have binary data, so +SED will cipher it
 *  -- Now we have ciphertext, so +URL will url encode it.
 *
 * We can send this to someone else, and they can put it in $cipher...
 *
 *	@newfd = open(newfile.txt W)
 *	@newdata = xform("-URL -SED +CTCP" password $cipher)
 *	@writeb($newfd $newdata)
 *	@close($newfd)
 *
 * We did the reverse of the above:
 *  -- We -URL to recover the binary data
 *  -- We -SED to decrypt it using the password
 *  -- We +CTCP to put it in a form we can use with $writeb().
 *
 * Viola!
 */
BUILT_IN_FUNCTION(function_xform, input)
{
#define MAX_TRANSFORMS 16
	char *	directives;
	int	x, i = 0;
	int	types[MAX_TRANSFORMS];
	char *	args[MAX_TRANSFORMS];
	int	encodings[MAX_TRANSFORMS];

	char *	typestr;
	int	type;
	int	encodingx;
	char *	arg;
	int	numargs;

	char	*srcbuf, *destbuf, *ptr;
	size_t	srcbuflen, destbuflen, ptrbuflen;
	size_t	refbuflen;
	int	expand, overh;

	/*
	 * First we have to calculate how many args there are before
	 * the original text.  There is one arg for the directives,
	 * and then we have to check each directive to see how many 
	 * args it takes.
	 */
	GET_DWORD_ARG(directives, input)
	while ((typestr = next_arg(directives, &directives)))
	{
	    if (*typestr == '+')
		typestr++, encodingx = 1;
	    else if (*typestr == '-')
		typestr++, encodingx = 0;
	    else
		encodingx = 1;

	    if ((type = lookup_transform(typestr, &numargs, &expand, &overh)) > -1)
	    {
		if (numargs == 1)
			GET_DWORD_ARG(arg, input)
		else
			arg = NULL;

		if (i < MAX_TRANSFORMS)
	        {
			types[i] = type;
			encodings[i] = encodingx;
			args[i] = arg;
			i++;
		}
	    }
	}

	refbuflen = strlen(input);
	srcbuf = (char *)new_malloc(refbuflen * 10 + 256);
	destbuf = (char *)new_malloc(refbuflen * 10 + 256);

	strlcpy(srcbuf, input, refbuflen + 1);
	srcbuflen = refbuflen;
	destbuflen = refbuflen * 10 + 256;

	for (x = 0; x < i; x++)
	{
		ptrbuflen = transform_string(types[x], encodings[x], args[x],
				srcbuf, srcbuflen, destbuf, destbuflen);
		ptr = srcbuf;
		srcbuf = destbuf;
		srcbuflen = ptrbuflen;
		destbuf = ptr;
		destbuflen = refbuflen * 10 + 256;
	}

	new_free(&destbuf);
	srcbuf[srcbuflen] = 0;
	RETURN_MSTR(srcbuf);
}

#ifdef HAVE_RUBY

BUILT_IN_FUNCTION(function_ruby, input)
{
	return rubyeval ( input );
}

#endif

BUILT_IN_FUNCTION(function_curcmd, unused) {
	RETURN_STR(current_command);
}

/* 
 * is8bit(s) returns the index number of the first character of string
 * s that is a 8-bit character. Possible use: "discovers" (?) Unicode or
 * whatever. Returns -1 if nil (0) 8-bit characters are found.
 */
BUILT_IN_FUNCTION(function_is8bit, input) {
	int n = 0, l;
	if (!input || !*input)
		RETURN_EMPTY;
	l = strlen (input);
	for (n = 0; n < l; n++)
		if (input[n] & 0x80)
			RETURN_INT(n);
	RETURN_INT(-1);
}

/* Returns the result of num_code_points(). D'oh! */
BUILT_IN_FUNCTION(function_isutf8, input)
{
	if (!input || !*input)
		RETURN_EMPTY;
	RETURN_INT(num_code_points(input));
}

/*
 * This function converts a delimited string (ie, $PATH) into a dword list.
 *  $splitw(: $PATH) -> /bin /usr/bin "/home/user/my programs/" ...
 */
BUILT_IN_FUNCTION(function_splitw, input)
{
	char *	delim_str;
	int	delim;
	char *	str;
	unsigned char **wordl;
	int 	wordc;
	char *	retval;

	/* The delimiter is the first code point in the first argument. */
	GET_DWORD_ARG(delim_str, input);
	delim = next_code_point(CUC_PP &delim_str, 0);

	if (!(wordc = new_split_string(input, &wordl, delim)))
		RETURN_EMPTY;

	/* unsplitw() disposes of "wordl" for us. */
	retval = unsplitw((char ***)&wordl, wordc, DWORD_YES);
	RETURN_MSTR(retval);
}

/*
 * Usage: $strptime("strftime format" textual string)
 * -- This function does the reverse of $strftime().  It converts 
 *    "textual string" into a $time() value, if and only if "textual string" 
 *    is the representation of that $time() value as created by strftime().  
 *    Got it? 
 */
BUILT_IN_FUNCTION(function_strptime, input)
{
	struct tm	timeptr;
	char *		format;
	time_t		the_time;

#ifdef HAVE_STRPTIME
	GET_DWORD_ARG(format, input);

	if (!(strptime(input, format, &timeptr)))
		RETURN_EMPTY;

	the_time = mktime(&timeptr);
	RETURN_INT(the_time);
#else
	RETURN_EMPTY;
#endif
}

/*
 * This function will return 1 if the argument is an expression that
 * appears to pass a basic syntax text.  The basic syntax test requires
 * that the first character either be a { or a ( and contain a matching
 * number of }s or )s.  We do this by calling my_next_expr() which is 
 * what the command parser does anyways.
 * 
 * What this function does NOT do is tell you whether or not what is 
 * inside your block makes any sense.  That's not possible to tell 
 * without running it.  All we can tell you is if it will be rejected
 * do to mismatched {} or ()s.
 */
BUILT_IN_FUNCTION(function_check_code, input)
{
	char type;

	while (input && *input && isspace(*input))
		input++;

	if (input == NULL || (*input != '{' && *input != '('))
		RETURN_INT(-1);		/* Not a block statement or expr */

	type = *input;
	if (!next_expr_failok(&input, type))
		RETURN_INT(-2);		/* Unmatched brace or paren */

	while (input && *input && isspace(*input))
		input++;

	if (input == NULL || *input)
		RETURN_INT(-3);		/* Stuff after trailing } */

	RETURN_INT(0);			/* Looks ok to me */
}


BUILT_IN_FUNCTION(function_channellimit, word)
{
	char	*channel;
	char    *booya = (char *) 0;
	int	limit;
	size_t	rvclue=0;

	do
	{
		channel = next_func_arg(word, &word);
		if ((!channel || !*channel) && booya)
			break;

		limit = get_channel_limit(channel, from_server);
		malloc_strcat_word_c(&booya, space, ltoa(limit), 
					DWORD_DWORDS, &rvclue);
	}
	while (word && *word);

	RETURN_MSTR(booya);
}


/*
 * Here is the "plan": we allow the user to either do: 
 *
 *   echo $xform(iconv FROMCODE/TOCODE[/OPTION] stuff) 
 *
 * which is heaps expensive, because it /opens/ and 
 * /closes/ an iconv_t descriptor for each usage. 
 *   Or the user can control this stuff herself, by first
 * opening the iconv descriptor manually, and then refering to
 * its identifier thusly:
 *
 *   @ id = xform(add FROMCODE/TOCODE[/OPTION])
 *   echo $xform(+iconv $id stuff)
 *
 * This will allow for a reversal of the encoding like:
 *   echo $xform(-iconv $id stuff)
 *
 * The identifier will be closed by doing:
 *
 *   @ iconvctl(remove $id)
 *
 * A list of (occupied) identiers will accessable by:
 *
 *   echo $iconvctl(list)
 *
 * And a specific identifiers specification will be available by:
 *
 *   echo $iconvctl(get $id)
 *
 * Other functionality may or may not be added at
 * a later point of time.
 */

char *function_iconvctl (char *input)
{
	size_t len, pos;
	char *listc;
	int id;
	int intarg;

	iconv_t forward, reverse;

	GET_FUNC_ARG(listc, input);
	len = strlen(listc);

	/*
	 * Add descriptor to the first available "slot",
	 * or extend the list by one, and use the last "slot".
	 */
	if (!my_strnicmp(listc, "ADD", len))
	{
		GET_FUNC_ARG(listc, input);
		if (my_iconv_open(&forward, &reverse, listc))
			RETURN_EMPTY;
		
		/* Do we need to initiate the list? */
		if (iconv_list_size == 0)
		{
			iconv_list = (struct Iconv_stuff **) new_malloc(sizeof(struct Iconv_stuff *));
			iconv_list_size = 1;
			iconv_list[0] = NULL;
		}

		/* Is there an empty "slot"? */
		for (id = 0; id < iconv_list_size; id++)
			if (iconv_list[id] == NULL)
				break;
		
		/* Do we need to realloc the list? */
		if (id == iconv_list_size)
		{
			iconv_list_size++;
			new_realloc((void **) &iconv_list, 
				sizeof(struct Iconv_stuff *) * iconv_list_size);	
		}
		
		/* Allocate a structure. */
		iconv_list[id] = (struct Iconv_stuff *) new_malloc(sizeof(struct Iconv_stuff));
		
		/* Do this stuff... */
		iconv_list[id]->forward = forward;
		iconv_list[id]->reverse = reverse;
		iconv_list[id]->stuff = malloc_strdup(listc);
		
		/* Return identifier. */
		RETURN_INT(id);
	}

	if (iconv_list_size == 0)
		RETURN_EMPTY;

	/*
	 * Return a list of used identifiers.
	 */
	if (!my_strnicmp(listc, "LIST", len)) 
	{
		size_t  clue = 0;
		char *retval = NULL;
		for (id = 0; id < iconv_list_size; id++)
			if (iconv_list[id] != NULL && iconv_list[id]->stuff != NULL)
				malloc_strcat_wordlist_c(&retval, space, ltoa(id), &clue);
		RETURN_MSTR(retval);
	}

	/* 
	 * Return list size.
	 */
	 if (!my_strnicmp(listc,"SIZE", len))
	 	RETURN_INT(iconv_list_size);

	/*
	 * Return the stuff from a given identifier; 
	 * 0 if NULL, and nothing if too high.
	 */
	if (!my_strnicmp(listc, "GET", len))
	{
		GET_INT_ARG(intarg, input);
		
		if (intarg >= iconv_list_size || intarg < 0)
			RETURN_EMPTY;
		if (iconv_list[(size_t) intarg] == NULL)
			RETURN_INT(0);
		RETURN_STR(iconv_list[(size_t) intarg]->stuff);
	}

	/*
	 * Remove a given identifier from the list, and
	 * truncate the identifier was the last of the list.
	 *
	 * Return 0 if unsuccessful, 1 if successful.
	 */
	if (!my_strnicmp(listc, "REMOVE", len))
	{
		GET_INT_ARG(intarg, input);
		if (intarg >= iconv_list_size || intarg < 0)
			RETURN_EMPTY;
		id = (size_t) intarg;
		if (iconv_list[id] == NULL && id + 1 != iconv_list_size)
			RETURN_INT(0);
		if (iconv_list[id] != NULL)
		{
			iconv_close(iconv_list[id]->forward);
			iconv_close(iconv_list[id]->reverse);
			new_free(&iconv_list[id]->stuff);
			new_free(&iconv_list[id]);
			iconv_list[id] = NULL;
		}

		/* Check the last slots. If 1 or more is "empty", realloc. */
		for (pos = iconv_list_size - 1;
			pos > 0 && iconv_list[pos] == NULL; pos--);
		if (((iconv_list_size -1) - pos) > 0)
		{
			iconv_list_size = pos + 1;
			if (iconv_list[iconv_list_size - 1] == NULL)
			{
				new_free(&iconv_list);
				iconv_list = NULL;
				iconv_list_size = 0;
			}
			else
			{
				new_realloc((void **) &iconv_list, iconv_list_size * 
					sizeof(struct Iconv_stuff *));
			}
		}
		RETURN_INT(1);
	}

	RETURN_EMPTY;
}

BUILT_IN_FUNCTION(function_channelsyncing, word)
{
	char *	channel;
	char *	serverstr;
	int	servref;
	int	retval;

	GET_FUNC_ARG(channel, word);
	GET_FUNC_ARG(serverstr, word);
	servref = str_to_servref(serverstr);

	retval = channel_is_syncing(channel, servref);
	RETURN_INT(retval);
}

#if 0
void	help_topics_commands(FILE *);
void	help_topics_functions(FILE *);
void	help_topics_scripts(FILE *);
void	help_topics_bind(FILE *);
void	help_topics_ctcp(FILE *);
void	help_topics_dcc(FILE *);
void	help_topics_on(FILE *);
void	help_topics_set(FILE *);
void	help_topics_window(FILE *);

void    help_topics_functions (FILE *f)
{
        int     x;

        for (x = 0; built_in_functions[x].name; x++)
                fprintf(f, "function %s\n", built_in_functions[x].name);
}

void	help_topics_scripts (FILE *f)
{
	char	dir[1024];
	DIR *	d;
	struct dirent *e;

	snprintf(dir, 1024, "../script");
	if (!(d = opendir(dir)))
	{
		yell("Could not open dir %s", dir);
		return;
	}

	while ((e = readdir(d)))
	{
		if (*e->d_name == '.')
			continue;
		fprintf(f, "script %s\n", e->d_name);
	}

	closedir(d);
}

BUILT_IN_FUNCTION(function_help_topics, word)
{
	FILE *f;

	if (!(f = fopen("help_topics.txt", "w")))
	{
		yell("Could not open \"help_topics.txt\"");
		RETURN_EMPTY;
	}

	help_topics_commands(f);
	help_topics_functions(f);
	help_topics_scripts(f);
	help_topics_bind(f);
	help_topics_ctcp(f);
	help_topics_dcc(f);
	help_topics_on(f);
	help_topics_set(f);
	help_topics_window(f);
	fclose(f);
	say("Done.  Check help_topics.txt");
	RETURN_EMPTY;
}
#endif

/*
 * $chankey(servref #channel) - return the +k key for a channel.
 *
 * Arguments:
 *  $0 - servrer - A server refnum
 *  $1 - #channel - A channel
 *
 * Return Value:
 *	empty_string	- Either 
 *			  1. 'refnum' not provided, or 
 *			  2. '#channel' not provided, or
 *			  3. You're not on '#channel' on 'refnum', or
 *			  4. #channel doesn't have a key (mode +k)
 *	anything else	- The mode +k key for #channel on server 'refnum'
 */
BUILT_IN_FUNCTION(function_chankey, input)
{
	int		refnum = -1;
	const char *	channel;
	const char *	key;

	GET_INT_ARG(refnum, input);
	GET_FUNC_ARG(channel, input);

	key = get_channel_key(channel, refnum);
	RETURN_STR(key);
}

#ifdef HAVE_PYTHON

BUILT_IN_FUNCTION(function_python, input)
{
	return python_eval_expression ( input );
}

BUILT_IN_FUNCTION(function_pydirect, input)
{
	const char *	py_funcname;

	GET_FUNC_ARG(py_funcname, input)
	return call_python_directly(py_funcname, input);
}

#endif

/*
 * $json_error() - show the parse error the from the last json_explode()
 *
 * Returns the parse error from the last $json_explode() call, or an empty
 * string if the last $json_explode() call succeeded.
 */
static char *json_last_error;

BUILT_IN_FUNCTION(function_json_error, input)
{
	RETURN_STR(json_last_error);
}

/*
 * $json_explode(var json) - deserialise a JSON string into an assign var
 *
 * Arguments:
 *  $0 - var - An assign variable
 *  $1 - json - A valid JSON string
 *
 * Returns an empty string on failure or 1 on success.
 */
static void explode_json_object(const char *var, cJSON *item)
{
	int n;
	cJSON *subitem;
	char *subvar = NULL;
	char *subitem_name = NULL;

	switch (item->type)
	{
		case cJSON_False:
		add_var_alias(var, "0", 0);
		break;

		case cJSON_True:
		add_var_alias(var, "1", 0);
		break;

		case cJSON_NULL:
		add_var_alias(var, NULL, 0);
		break;

		case cJSON_Number:
		add_var_alias(var, ftoa(item->valuedouble), 0);
		break;

		case cJSON_String:
		add_var_alias(var, item->valuestring, 0);
		break;

		case cJSON_Array:
		case cJSON_Object:
		for (subitem = item->child, n = 0; subitem; subitem = subitem->next, n++)
		{
			if (subitem->string)
			{
				char *ptr = malloc_strcpy(&subitem_name, subitem->string);
		
				/* The name must be mangled to be suitable as an ASSIGN */
				while (*ptr)
				{
					if (!isalnum(*ptr))
						*ptr = '_';

					ptr++;
				}

				malloc_sprintf(&subvar, "%s.%s", var, subitem_name);
			}
			else
				malloc_sprintf(&subvar, "%s.%d", var, n);


			explode_json_object(subvar, subitem);
		}
		break;

		default:
		yell("Odd cJSON type in $json_explode");
	}
	new_free(&subitem_name);
	new_free(&subvar);
}	

BUILT_IN_FUNCTION(function_json_explode, input)
{
	const char *var;
	cJSON *root;

	GET_FUNC_ARG(var, input);
	root = cJSON_Parse(input);

	if (!root)
	{
		malloc_sprintf(&json_last_error, "JSON parse error at position %u", (unsigned)(cJSON_GetErrorPtr() - input));
		RETURN_EMPTY;
	}

	new_free(&json_last_error);
	explode_json_object(var, root);
	cJSON_Delete(root);
	RETURN_INT(1);
}

/*
 * $json_implode(var) - serialise a Structure assign var into a JSON string
 *
 * Arguments:
 *  $0 - var - An assign variable
 *
 * Returns an empty string on failure or a valid JSON string on success.
 */
static int implode_struct_var(char *var, cJSON *parent)
{
	char **sublist;
	int count;
	int i;
	const char *name = strrchr(var, '.');
	int err = 0;

	if (name)
		name++;
	else
		name = var;

	sublist = get_subarray_elements(var, &count, VAR_ALIAS);

	if (count == 0)
	{
		char *var_text = get_variable(var);
		cJSON_AddStringToObject(parent, name, var_text);
		new_free(&var_text);
	}
	else
	{
		cJSON *child = cJSON_CreateObject();

		if (child)
		{
			cJSON_AddItemToObject(parent, name, child);

			for (i = 0; i < count && err == 0; i++)
				err = implode_struct_var(sublist[i], child);
		}
		else
		{
			yell("cJSON_CreateObject() returned NULL");
			err = ENOMEM;
		}
	}

	for (i = 0; i < count; i++)
		new_free(&sublist[i]);
	new_free(&sublist);
	return err;
}

BUILT_IN_FUNCTION(function_json_implode, input)
{
	const char *var = NULL;
	const char *canon_var;
	cJSON *root;
	char **sublist;
	int count;
	int i;
	char *json = NULL;
	char *retval;
	int err = 0;
	int	compact = 0;

	char *name1 = NULL, *flag = NULL;
	struct kwargs kwargs[] = {
		{ "root", KWARG_TYPE_STRING, &var, 1 },
		{ "compact", KWARG_TYPE_BOOL, &compact, 1 },
		{ NULL, KWARG_TYPE_SENTINAL, NULL, 0 }
	};

	if (input && *input == '{')
		parse_kwargs(kwargs, input);
	else
		GET_FUNC_ARG(var, input);

	RETURN_IF_EMPTY(var);
	canon_var = remove_brackets(var, NULL);


	sublist = get_subarray_elements(canon_var, &count, VAR_ALIAS);
	new_free(&canon_var);
	
	if (count == 0)
		RETURN_EMPTY;

	root = cJSON_CreateObject();

	if (root)
	{
		for (i = 0; i < count && err == 0; i++)
			err = implode_struct_var(sublist[i], root);

		if (err == 0)
		{
			if (compact)
				json = cJSON_PrintUnformatted(root);
			else
				json = cJSON_Print(root);
		}
		cJSON_Delete(root);
	}

	for (i = 0; i < count; i++)
		new_free(&sublist[i]);
	new_free(&sublist);

	retval = malloc_strdup(json);

	if (json)
		cJSON_free(json);

	return retval;
}

BUILT_IN_FUNCTION(function_uuid4, input)
{
	char *retval;
	char *var;

	if (!my_stricmp(input, "NODASHES"))
		retval = uuid4_generate_no_dashes();
	else
		retval = uuid4_generate();

	RETURN_MSTR(retval);	/* Never pass function call to RETURN_* */
}

BUILT_IN_FUNCTION(function_execctl, input)
{
	char *retval;
	retval = execctl(input);	
	RETURN_MSTR(retval);	/* Never pass function call to RETURN_* */
}

BUILT_IN_FUNCTION(function_cp437test, input)
{
	unsigned char 	my_string[257];
	size_t		my_string_len;
	char *		result;
	size_t		resultlen = 0;
	unsigned int	i;

	for (i = 0; i < 256; i++)
		my_string[i] = (unsigned char) i + 1;
	my_string[256] = 0;

	my_string_len = 257;
	result = cp437_to_utf8(my_string, my_string_len, &resultlen);
	say("%s", result);
	RETURN_MSTR(result);
}

BUILT_IN_FUNCTION(function_jsontest, input)
{
	char *name1 = NULL, *flag = NULL;
	struct kwargs kwargs[] = {
		{ "name1", KWARG_TYPE_STRING, &name1, 1 },
		{ "flag", KWARG_TYPE_STRING, &flag, 1 },
		{ NULL, KWARG_TYPE_SENTINAL, NULL, 0 }
	};

	if (input && *input == '{')
		parse_kwargs(kwargs, input);
	else
		say("jsontest: invalid input (must be json object)");

	if (name1)
		say("name1 is %s", name1);
	if (flag)
		say("flag is %s", flag);
	RETURN_EMPTY;
}

#define HEX2NUM(x)   ( (x) >= '0' && (x) <= '9' ? (x) - '0'   \
					        : (x) >= 'A' && (x) <= 'F' ? (x) - 'A' + 10  \
                                                                           : 0 )

BUILT_IN_FUNCTION(function_rgb, input)
{
	intmax_t	r = 0, g = 0, b = 0;
	int	attr = 1;
	int	color;
	char	retval_str[4];

	while (input && *input && my_isspace(*input))
		input++;

	if (input && *input == '{')
	{
		struct kwargs kwargs[] = {
			{ "r", KWARG_TYPE_INTEGER, &r, 1 },
			{ "g", KWARG_TYPE_INTEGER, &g, 1 },
			{ "b", KWARG_TYPE_INTEGER, &b, 1 },
			{ "attr", KWARG_TYPE_BOOL, &attr, 0 },
			{ NULL, KWARG_TYPE_SENTINAL, NULL, 0 }
		};

		parse_kwargs(kwargs, input);
	}
	else if (*input == '#') 
	{
		char r1 = 0, r2 = 0, g1 = 0, g2 = 0, b1 = 0, b2 = 0;

		input++;
#define next_byte_or_fail(src, var)   if (* src ) var = * src ++; else RETURN_EMPTY;
		next_byte_or_fail(input, r1)
		next_byte_or_fail(input, r2)
		next_byte_or_fail(input, g1)
		next_byte_or_fail(input, g2)
		next_byte_or_fail(input, b1)
		next_byte_or_fail(input, b2)

		r = HEX2NUM(r1) * 16 + HEX2NUM(r2);
		g = HEX2NUM(g1) * 16 + HEX2NUM(g2);
		b = HEX2NUM(b1) * 16 + HEX2NUM(b2);
	}
	else 
	{
		GET_INT_ARG(r, input);
		GET_INT_ARG(g, input);
		GET_INT_ARG(b, input);
	}

	color = rgb_to_256(r, g, b);

	if (attr)
		snprintf(retval_str, 4, COLOR256_TAG_STR "%X", color);
	else
		snprintf(retval_str, 3, "%X", color);

	RETURN_STR(retval_str);
}


