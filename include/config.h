/* 
 * 'new' config.h:
 *	A configuration file designed to make best use of the abilities
 *	of ircII, and trying to make things more intuitively understandable.
 *
 * Original: Michael Sandrof
 * V2 by Carl V. Loesch (lynx@dm.unirm1.it)
 * V2.EPIC by jfn (jnelson@acronet.net)
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

/*
 * Notes to the unwary:
 *
 *  -- You cant just add a ``#define DEFAULT_X'' and expect it to work.
 *     Those things that do not have defaults are that way on purpose.
 *     Either they have their own defaults, or a default is inappropriate.
 *
 *  -- Unless the description expliclity says that the #define is optional,
 *     you may NOT remove it or #undef it, else the client will not compile 
 *     properly.
 */


/* 
 *  This is where you define a list of ``fallback'' servers in case the client
 *  cannot under other circumstances figure out where to connect.   Normally,
 *  the server to use is determined by the ``SERVERS_FILE'' (see below), or
 *  by a server specified on the command line.   But if for some reason your
 *  ``SERVERS_FILE'' isnt there or isnt readable, or the user doesnt specify
 *  a server, then this list will be used.
 *
 *  The list should be a space seperated list of the form 
 *  hostname:portnum:password.  The portnum and password are optional.  
 *  An example is:
 *
 *	#define DEFAULT_SERVER "irc.iastate.edu irc-2.mit.edu:6666:lag-2sux"
 *
 *  THIS DEFINE IS -!-NOT-!- OPTIONAL.  You must provide a fallback list or
 *  the client will NOT compile and work properly!  Use the default here if
 *  you dont have other servers to use.
 */
#define DEFAULT_SERVER "localhost irc.efnet.net irc.undernet.org irc.dal.net"

/*
 * The left and right brace characters ('{', '}') are special characters in
 * the IRC character set, becuase they represent scandanavian characters and
 * are not expected to be treated any differently than any other alphanumeric
 * character.  When this is #define'd, the client makes a best guess attempt
 * to determine if a { is being used as a brace, or as an alphanumeric char.
 *
 * The way it determines is simple:
 *	An lbrace ('{') is a normal character UNLESS ONE OF THE FOLLOWING:
 *		* It is the first character on a line
 *		* It is preceeded by whitespace
 *		* It is preceeded by a right paren (')')
 *		* It is preceeded by a right brace ('}')
 * Similarly,
 *	An rbrace ('}') is a normal character UNLESS ONE OF THE FOLLOWING:
 *		* It is the last character on a line
 *		* It is followed by whitespace
 *		* It is followed by a left paren ('(')
 *		* It is followed by a left brace ('{')
 *
 * If the following is #define'd, the above rules are in effect.  If the
 * following is #undef'd, then the normal, traditional rules apply and every
 * { and } is counted for bracing purposes.
 *
 * Some day, if this works out, this will become the default behavior -- 
 * but not today.
 */
#undef BRACE_LOAD_HACK

/*
 * A recent change was to make the %S status line expando always display
 * the server information, even if you were connected to only one server.
 * However, a few people expressly dislike this change and want the old 
 * behavior.  If this is #define'd, you get the old behavior.  If this is
 * #undef'd, you get the new behavior.
 */
#define OLD_STATUS_S_EXPANDO_BEHAVIOR

/*
 * Oooops!  This was supposed to be here, but it never did make it.  Which
 * explains a lot.  Anyhow, #define this if you want to have the option of
 * doing floating point math in EPIC.  #undef this if you always want integer
 * operations to always be done.  The client can still do integer operations 
 * if this is defined, just /set floating_point_math OFF.
 *
 * Note that if this is defined, the client actually will do all of your
 * operations in floating point and then the *result* will be truncated.
 * This is different from when this is #undef when *each term* will be 
 * truncated.  For example:
 *
 * 	#undef'd  4.2 - 2.6 	-> (4 - 2) -> 2
 *	#define'd 4.2 - 2.6     -> 1.6 -> 1
 */
#define FLOATING_POINT_SUPPORT

/*
 * Undef this if your FIONREAD is not useful.  At this time, it makes very
 * little difference because we dont use FIONREAD for anything useful.  But
 * nevertheless, if youre allergic to mild bsd-isms, then you can #undef this.
 */
#define HAVE_USEFUL_FIONREAD

/*
 * This sets how you want to see the 368 numeric to be hooked.  The default
 * (#undef) is the traditional EPIC behavior.
 *		EPIC		ircII
 *	$0	server		server
 *	$1	number of bans	channel name
 *	$2	channel name	<nothing -- now number of bans>
 */
#undef IRCII_LIKE_BAN_SUMMARY

/*
 * When this is define'd, the -z flag, the IRCUSER and USER environment 
 * variables, as well as the /IRCUSER command will be honored.  This is 
 * not nearly as big a deal as it used to be, because every server uses 
 * identd and ignores the client-specified username.  There is no compelling 
 * reason for any site using identd to forbid this behavior by default.  
 * Any site that wont run identd i dont have a lot of sympathy for.
 */
#define ALLOW_USER_SPECIFIED_LOGIN

/*
 * I moved this here because it seemed to be the most appropriate
 * place for it.  Define this if you want support for ``/window create''
 * and its related features.  If you dont want it, youll save some code,
 * and you wont need 'wserv', and if you do want it, you can have it in
 * all of its broken glory.
 */
#define WINDOW_CREATE

/*
 * Define this if you want an mIRC compatable /dcc resume capability.
 * Note that this BREAKS THE IRC PROTOCOL, and if you use this feature,
 * the behavior is NON COMPLIANT.  If this warning doesnt bother you,
 * and you really want this feature, then go ahead and #define this.
 *
 * Unfortunately, due to popular pressure, im having to #define this by
 * default.  The capability wont be turned on, however, unless you also
 * do a /set mirc_broken_dcc_resume on,   which is OFF by default.  No,
 * there will not be a way to default it to ON short of modifying vars.c.
 * This is a comprimise, and i wont give any further.  Those who dont want
 * this feature can #undef this, or can hook /on set "mirc_broken_dcc_resume".
 */
#define MIRC_BROKEN_DCC_RESUME

/*
 * Youll want to define this if your system is missing the glob()
 * call, or if its broken (solaris).
 *
 * Actually, you should #define this if you can compile the supplied
 * glob.c.  If it works, dont mess with it.
 */
#define NEED_GLOB

/*
 *  ircII has a security feature for paranoid sysadmins or for those
 *  users whose sysadmins are paranoid.  The security feature allows
 *  you to restrict who may use the ircII client.  You have four options:
 *
 *	1) compile into the binary a list of uids who can use the program
 *		*Pros: cant be hacked -- very secure
 *		*Cons: cant be changed w/o recompiling
 *	2) compile into the binary a file which will contain the uids of
 *	   the people who can use the program
 *		*Pros: can be changed as you need by just editing the file
 *		*Cons: since the uids are in a file, prone to hacking
 *	3) compile into the binary a file which will contain the uids of
 *	   the people who cannot use the program
 *		*Pros: allows for public use and allow you to exclude
 *		       troublemakers without enumerating everyone else
 *		*Cons: since the uids are in a file, prone to hacking
 *	4) compile into the binary a password
 *		*Pros: cant be hacked -- secure
 *		*Cons: cant be changed w/o recompiling
 *
 *    The first two options are mutually exclusive.  The third and fourth
 *    options can be specified at your option.  If you specify both the
 *    first and second options, the first option has precedence.
 */

/*
 *   To use the first security feature, #define HARD_SECURE.  You will also
 *   have to #define VALID_UIDS which will be a list of those uids (integers,
 *   not usernames) which will be allowed to execute the resulting program.
 *   If you #define HARD_SECURE but do not define #VALID_UIDS, then noone
 *   will be able to execute the program!
 */
#undef HARD_SECURE
#define VALID_UIDS "100 101"

/*
 *  To use the second security measure, simply #define SOFT_SECURE to a 
 *  filename that will be world-readable that will contain the uids of
 *  all the users who will be allowed to execute the program.  It is important
 *  that this file be readable by at least every person who can execute the
 *  program or this security measure will be comprimised.
 *
 *  The uid file should have one uid per line (integer, not username).
 *
 *  You can define VALID_UID_FILE, but if SOFT_SECURE is not defined, it will
 *  not be used.
 */
#undef SOFT_SECURE
#define VALID_UID_FILE "/home/jnelson/..."

/*
 *  This allows you to use the third security option.  If you define this,
 *  it should be assigned to a file that will contain a listing of all of
 *  the uids (integers, not usernames) that will not be allowed to execute
 *  the resulting program.
 */
/*#define INVALID_UID_FILE "/home/jnelson/...."*/

/*
 * This part lets you deny certain hosts from running your irc client.
 * For instance, my university does not allow irc'ing from dialup machines,
 * So by putting the dialup's hostnames in the specified file, they can't
 * Run irc.
 *  -- Chris Mattingly <Chris_Mattingly@ncsu.edu>
 *
 * If you define this, it *absolutely* must be in double quotes ("s)!
 */
#undef HOST_SECURE

#ifdef HOST_SECURE
#define INVALID_HOST_FILE "/home/jnelson/...host.deny"
#endif


/*
 *  This allows you to use the fourth security option.  If you define this,
 *  the program will prompt the user to enter this prompt before it will
 *  continue executing the program.  This password does not affect in any
 *  way the other protection schemes.  A user who is not allowed to run
 *  the program will not be allowed to use the program even if they know
 *  the password.
 */
/*#define PASSWORD "booya"*/

/*
 * This is the fun part.  If someone runs your program who shouldnt run
 * it, either because the dont know the password or because they arent
 * on the valid list or whatever, ircII will execute this program to 
 * "spoof" them into thinking your program is actually some other program.
 *
 * This can be defined to any valid C expression that will resolve to a
 * character string. (ie, a character literal or function call)
 */
#define SPOOF_PROGRAM getenv("SHELL")

/* 
 * If you define UNAME_HACK, the uname information displayed in the
 * CTCP VERSION info will appear as "*IX" regardless of any other
 * settings.  Useful for paranoid users who dont want others to know
 * that theyre running a buggy SunOS machine. >;-)
 */
#undef UNAME_HACK


/* And here is the port number for default client connections.  */
#define IRC_PORT 6667

/*
 * If you want to have a file containing the list of irc servers to 
 * use, define SERVERS_FILE to be that filename.  Put the file in the 
 * ircII library directory.  This file should be whitespace seperated
 * hostname:portnum:password (with the portnum and password being
 * optional).  This server list will supercede the DEFAULT_SERVER
 */
#define SERVERS_FILE "ircII.servers"


/*
 * define this if you want your irc client to exit after an 
 * operator kill.  I have no idea why you would, though.
 */
#undef QUIT_ON_OPERATOR_KILL

/*
 * The compile sequence records the user/host/time of the compile,
 * which can be useful for tampering and newbie reasons.  If you want
 * the compile to remain anonymous, define this option.  In this case,
 * the host and the time will remain, but the 'user' field will not
 * be displayed to the user.
 * 
 * Please dont define this on a whim -- be sure you really want it.
 */
#undef ANONYMOUS_COMPILE

/*
 * The /LOAD path is now generated at runtime, rather than at compile time.
 * This is to allow you to change IRCLIB and have its script library be
 * resepected without having to change IRCPATH as well.  This is a printf
 * format of what the default load path is to be.  The %s format indicates
 * the runtime IRCLIB value.  This value is only used at startup time.
 */
#define DEFAULT_IRCPATH "~/.epic:~/.irc:%s/script:."


/*
 * Below are the IRCII variable defaults.  For boolean variables, use 1 for
 * ON and 0 for OFF.  You may set string variable to NULL if you wish them to
 * have no value.  None of these are optional.  You may *not* comment out or
 * remove them.  They are default values for variables and are required for
 * proper compilation.
 */
#define DEFAULT_ALLOW_C1_CHARS 0
#define DEFAULT_ALT_CHARSET 1
#define DEFAULT_ALWAYS_SPLIT_BIGGEST 1
#define DEFAULT_AUTO_NEW_NICK 1
#define DEFAULT_AUTO_RECONNECT 1
#define DEFAULT_AUTO_RECONNECT_DELAY 0
#define DEFAULT_AUTO_REJOIN 1
#define DEFAULT_AUTO_REJOIN_CONNECT 1
#define DEFAULT_AUTO_REJOIN_DELAY 0
#define DEFAULT_AUTO_UNMARK_AWAY 0
#define DEFAULT_AUTO_WHOWAS 1
#define DEFAULT_BAD_STYLE 0
#define DEFAULT_BANNER "***"
#define DEFAULT_BEEP 1
#define DEFAULT_BEEP_MAX 3
#define DEFAULT_BEEP_ON_MSG "NONE"
#define DEFAULT_BEEP_WHEN_AWAY 1
#define DEFAULT_BLINK_VIDEO 1
#define	DEFAULT_BOLD_VIDEO 1
#define DEFAULT_CHANNEL_NAME_WIDTH 0
#define DEFAULT_CLOCK 1
#define DEFAULT_CLOCK_24HOUR 0
#define DEFAULT_CLOCK_FORMAT NULL
#define DEFAULT_CLOCK_INTERVAL 60
#define DEFAULT_CMDCHARS "/"
#define DEFAULT_COLOR 1
#define DEFAULT_COMMAND_MODE 0
#define DEFAULT_COMMENT_HACK 1
#define DEFAULT_CONNECT_TIMEOUT 30
#define DEFAULT_CONTINUED_LINE "+"
#define DEFAULT_CPU_SAVER_AFTER 0
#define DEFAULT_CPU_SAVER_EVERY 60
#define DEFAULT_CURRENT_WINDOW_LEVEL NULL
#define DEFAULT_DCC_AUTO_SEND_REJECTS 1
#define DEFAULT_DCC_LONG_PATHNAMES 1
#define DEFAULT_DCC_SLIDING_WINDOW 1
#define DEFAULT_DCC_TIMEOUT 3600
#define DEFAULT_DISPATCH_UNKNOWN_COMMANDS 0
#define DEFAULT_DISPLAY 1
#define DEFAULT_DISPLAY_ANSI 1
#define DEFAULT_DISPLAY_PC_CHARACTERS 4
#define DEFAULT_DO_NOTIFY_IMMEDIATELY 1
#define DEFAULT_EIGHT_BIT_CHARACTERS 1
#define DEFAULT_EXEC_PROTECTION 0
#define DEFAULT_FLOATING_POINT_MATH 0
#define DEFAULT_FLOATING_POINT_PRECISION 16
#define DEFAULT_FLOOD_AFTER 3
#define DEFAULT_FLOOD_IGNORE 0
#define DEFAULT_FLOOD_MASKUSER 0
#define DEFAULT_FLOOD_RATE 3
#define DEFAULT_FLOOD_RATE_PER 1
#define DEFAULT_FLOOD_USERS 3
#define DEFAULT_FLOOD_WARNING 0
#define DEFAULT_FULL_STATUS_LINE 1
#define DEFAULT_HELP_PAGER 1
#define DEFAULT_HELP_PROMPT 1
#define DEFAULT_HELP_WINDOW 0
#define DEFAULT_HIDE_PRIVATE_CHANNELS 0
#define DEFAULT_HIGH_BIT_ESCAPE 2
#define DEFAULT_HIGHLIGHT_CHAR "BOLD"
#define DEFAULT_HISTORY 150
#define DEFAULT_HISTORY_CIRCLEQ 1
#define DEFAULT_HOLD_INTERVAL 10
#define DEFAULT_HOLD_MODE 0
#define DEFAULT_INDENT 0
#define DEFAULT_INPUT_ALIASES 0
#define DEFAULT_INPUT_PROMPT "> "
#define DEFAULT_INPUT_PROTECTION 1
#define DEFAULT_INSERT_MODE 1
#define DEFAULT_INVERSE_VIDEO 1
#define DEFAULT_KEY_INTERVAL 1000
#define DEFAULT_LASTLOG 256
#define DEFAULT_LASTLOG_LEVEL "ALL"
#define DEFAULT_LOG 0
#define DEFAULT_LOGFILE "irc.log"
#define DEFAULT_MAIL 2
#define DEFAULT_MAIL_INTERVAL 60
#define DEFAULT_MAX_RECONNECTS 4
#define DEFAULT_METRIC_TIME 0
#define DEFAULT_MODE_STRIPPER 0
#define DEFAULT_ND_SPACE_MAX 160
#define DEFAULT_NEW_SERVER_LASTLOG_LEVEL "ALL,-DCC"
#define DEFAULT_NO_CTCP_FLOOD 1
#define DEFAULT_NO_FAIL_DISCONNECT 0
#define DEFAULT_NOTIFY 1
#define DEFAULT_NOTIFY_INTERVAL 60
#define DEFAULT_NOTIFY_LEVEL "ALL"
#define DEFAULT_NOTIFY_ON_TERMINATION 1
#define DEFAULT_NOTIFY_USERHOST_AUTOMATIC 1
#define DEFAULT_NO_CONTROL_LOG 0
#define DEFAULT_NUM_OF_WHOWAS 1
#define DEFAULT_PAD_CHAR ' '
#define DEFAULT_QUIT_MESSAGE "ircII %s -- Are we there yet?"
#define DEFAULT_RANDOM_LOCAL_PORTS 0
#define DEFAULT_RANDOM_SOURCE 0
#define DEFAULT_REVERSE_STATUS_LINE 1
#define DEFAULT_SCROLLBACK 256
#define DEFAULT_SCROLLBACK_RATIO 50
#define DEFAULT_SCROLL_LINES 1
#define DEFAULT_SECURITY 0
#define DEFAULT_SHELL "/bin/sh"
#define DEFAULT_SHELL_FLAGS "-c"
#define DEFAULT_SHELL_LIMIT 0
#define DEFAULT_SHOW_CHANNEL_NAMES 1
#define DEFAULT_SHOW_END_OF_MSGS 1
#define DEFAULT_SHOW_NUMERICS 0
#define	DEFAULT_SHOW_STATUS_ALL 0
#define DEFAULT_SHOW_WHO_HOPCOUNT 1
#define DEFAULT_STATUS_AWAY " (Away)"
#define DEFAULT_STATUS_CHANNEL " %C"
#define DEFAULT_STATUS_CHANOP "@"
#define DEFAULT_STATUS_HALFOP "%"
#define DEFAULT_STATUS_SSL_ON "*SSL*"
#define DEFAULT_STATUS_SSL_OFF "*RAW*"
#define DEFAULT_STATUS_CLOCK " %T"
#define DEFAULT_STATUS_CPU_SAVER " (%L)"
#define DEFAULT_STATUS_FORMAT "%T [%R] %*%=%@%N%#%S%H%B%Q%A%C%+%I%O%M%F%L %D %U %W"
#define DEFAULT_STATUS_FORMAT1 "%T [%R] %*%=%@%N%#%S%H%B%Q%A%C%+%I%O%M%F%L %U "
#define DEFAULT_STATUS_FORMAT2 "%W %X %Y %Z "
#define DEFAULT_STATUS_HOLD " Held: "
#define DEFAULT_STATUS_HOLD_LINES "%B"
#define DEFAULT_STATUS_INSERT ""
#define DEFAULT_STATUS_MODE " (+%+)"
#define DEFAULT_STATUS_MAIL " (Mail: %M)"
#define DEFAULT_STATUS_NICK "%N"
#define DEFAULT_STATUS_NO_REPEAT 0
#define	DEFAULT_STATUS_NOTIFY " (W: %F)"
#define DEFAULT_STATUS_OPER "*"
#define DEFAULT_STATUS_OVERWRITE " (Overwrite)"
#define DEFAULT_STATUS_QUERY " (Query: %Q)"
#define DEFAULT_STATUS_SCROLLBACK " (Scroll)"
#define DEFAULT_STATUS_SERVER " (%S)"
#define DEFAULT_STATUS_TRUNCATE_RHS 1
#define DEFAULT_STATUS_UMODE " (+%#)"
#define DEFAULT_STATUS_USER "ircII-EPIC4 -- Type /help for help"
#define DEFAULT_STATUS_USER1 ""
#define DEFAULT_STATUS_USER2 ""
#define DEFAULT_STATUS_USER3 ""
#define DEFAULT_STATUS_USER4 ""
#define DEFAULT_STATUS_USER5 ""
#define DEFAULT_STATUS_USER6 ""
#define DEFAULT_STATUS_USER7 ""
#define DEFAULT_STATUS_USER8 ""
#define DEFAULT_STATUS_USER9 ""
#define DEFAULT_STATUS_USER10 ""
#define DEFAULT_STATUS_USER11 ""
#define DEFAULT_STATUS_USER12 ""
#define DEFAULT_STATUS_USER13 ""
#define DEFAULT_STATUS_USER14 ""
#define DEFAULT_STATUS_USER15 ""
#define DEFAULT_STATUS_USER16 ""
#define DEFAULT_STATUS_USER17 ""
#define DEFAULT_STATUS_USER18 ""
#define DEFAULT_STATUS_USER19 ""
#define DEFAULT_STATUS_USER20 ""
#define DEFAULT_STATUS_USER21 ""
#define DEFAULT_STATUS_USER22 ""
#define DEFAULT_STATUS_USER23 ""
#define DEFAULT_STATUS_USER24 ""
#define DEFAULT_STATUS_USER25 ""
#define DEFAULT_STATUS_USER26 ""
#define DEFAULT_STATUS_USER27 ""
#define DEFAULT_STATUS_USER28 ""
#define DEFAULT_STATUS_USER29 ""
#define DEFAULT_STATUS_USER30 ""
#define DEFAULT_STATUS_USER31 ""
#define DEFAULT_STATUS_USER32 ""
#define DEFAULT_STATUS_USER33 ""
#define DEFAULT_STATUS_USER34 ""
#define DEFAULT_STATUS_USER35 ""
#define DEFAULT_STATUS_USER36 ""
#define DEFAULT_STATUS_USER37 ""
#define DEFAULT_STATUS_USER38 ""
#define DEFAULT_STATUS_USER39 ""
#define DEFAULT_STATUS_VOICE "+"
#define DEFAULT_STATUS_WINDOW "^^^^^^^^"
#define DEFAULT_SUPPRESS_FROM_REMOTE_SERVER 0
#define DEFAULT_SUPPRESS_SERVER_MOTD 0
#define DEFAULT_SWITCH_CHANNEL_ON_PART 1
#define DEFAULT_SWITCH_CHANNELS_BETWEEN_WINDOWS 1
#define DEFAULT_TAB 1
#define	DEFAULT_TAB_MAX 0
#define DEFAULT_TERM_DOES_BRIGHT_BLINK 0
#define DEFAULT_UNDERLINE_VIDEO 1
#define DEFAULT_USERINFO "EPIC4 -- Get your laundry brighter, not just whiter!"
#define DEFAULT_VERBOSE_CTCP 1
#define DEFAULT_WORD_BREAK ".,; \t"
#define DEFAULT_XTERM "xterm"
#define DEFAULT_XTERM_OPTIONS NULL

/*
 * People have wanted me to explain some of these #defines.  Well,
 * Ill tell you what i will do.  I will tell you that some of these
 * defines turn on obscure features, and others turn on features that
 * are specificly placed there at the request of one of the debuggers,
 * but i am making the option of using it available to the general 
 * public.  You should always be aware of what changing one of these 
 * #defines might do to affect the operation of the client.  You can get
 * a good feel for the impact by grepping the source code for them.
 * General "themes" of what the defines do are listed on the right.  
 * These "themes" describe the *spirit* of the define, but do NOT 
 * annotate every reprocussion of defining it!
 *
 * Also, i dont guarantee that changing any of these defines will
 * or wont compile correctly, so you may have to be prepared to do
 * some minor debugging in that case (send patches along to me if
 * you do =)  Dont change any of these unless you know what it will do.
 */
#undef EMACS_KEYBINDINGS	/* meta-key keybindings. */
#undef EPIC_DEBUG		/* force coredump on panic */
#define EXEC_COMMAND		/* allow /exec comamnd */
#undef HACKED_DCC_WARNING	/* warn if handshake != sender */
#undef HARD_UNFLASH		/* do a hard reset instead of soft on refresh */
#undef NO_BOTS			/* no bots allowed */
#undef NO_CHEATING		/* always do it the "right" way, no shortcuts */
#undef STRIP_EXTRANEOUS_SPACES	/* strip leading and trailing spaces */

#undef	I_DONT_TRUST_MY_USERS	/* There are certain things that the stock
				   ircII client doesnt allow users to do
				   that are neither illegal by the letter of
				   the protocol nor the spirit of the protocol.
				   These are the things that only a really
				   anal retentive person would want to totaly
				   prohibit his users from doing without any
				   exceptions.  When i find these things, i
				   #ifdef them out under this define.  The
				   specific list of what may or may not be
				   contained under this define can change
				   from release to release.  This replaces
				   the I_AM_A_FASCIST_BASTARD define which
				   several people found offensive because
				   they wanted to define it =) */

/* Dont change these -- theyre important. */
#if defined(VALID_UIDS) && !defined(HARD_SECURE)
#undef VALID_UIDS
#endif

#if defined(PASSWORD) && !defined(HARD_SECURE) && !defined(SOFT_SECURE)
#undef PASSWORD
#endif

#if defined(HARD_SECURE) && !defined(VALID_UIDS)
#error You must #define VALID_UIDS if you #define HARD_SECURE
#endif

#if defined(VALID_UID_FILE) && !defined(SOFT_SECURE)
#undef VALID_UID_FILE
#endif

#if defined(SOFT_SECURE) && !defined(VALID_UID_FILE)
#error You must #define VALID_UID_FILE if you #define SOFT_SECURE
#endif

#ifndef SPOOF_PROGRAM
#define SPOOF_PROGRAM "/bin/sh"
#endif
/* end of section not to change */

#undef ALLOC_DEBUG

#endif /* _CONFIG_H_ */

