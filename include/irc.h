/*
 * irc.h: header file for all of ircII! 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1991 Troy Rollo
 * Copyright 1994 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __irc_h__
#define __irc_h__

#define IRCII_COMMENT   "Accept no limitations."
#define IRCRC_NAME 	"/.ircrc"
#define EPICRC_NAME 	"/.epicrc"
#define EMAIL_CONTACT 	"list@epicsol.org"

/*
 * Here you can set the in-line quote character, normally backslash, to
 * whatever you want.  Note that we use two backslashes since a backslash is
 * also C's quote character.  You do not need two of any other character.
 */
#define QUOTE_CHAR '\\'

#include "defs.h"
#include "config.h"
#include "irc_std.h"
#include "debug.h"

/* these define what characters do, inverse, underline, bold and all off */
/*	
 *	CAUTION CAUTION CAUTION CAUTION CAUTION CAUTION CAUTION
 *	CAUTION						CAUTION
 *	CAUTION		DONT CHANGE THESE!		CAUTION
 *	CAUTION						CAUTION
 *	CAUTION CAUTION CAUTION CAUTION CAUTION CAUTION CAUTION
 *
 * If you want to change the key bindings for your highlight characters,
 * then please use /bind .. BOLD, /bind .. REVERSE, /bind .. UNDERLINE, 
 * or /bind .. HIGHLIGHT_OFF.  These are REQUIRED to be set to the way
 * they are now, as the keybindings simply change whatever you bind these
 * to these actual values, so if you change these, it will break how
 * other people see your messages!
 */
#define REV_TOG		'\026'		/* ^V */
#define REV_TOG_STR	"\026"
#define UND_TOG		'\037'		/* ^_ */
#define UND_TOG_STR	"\037"
#define BOLD_TOG	'\002'		/* ^B */
#define BOLD_TOG_STR	"\002"
#define BLINK_TOG	'\006'		/* ^F (think flash) */
#define BLINK_TOG_STR	"\006"
#define ALL_OFF		'\017'		/* ^O */
#define ALL_OFF_STR	"\017"
#define ROM_CHAR	'\022'		/* ^R */
#define ROM_CHAR_STR	"\022"
#define ND_SPACE	'\023'		/* ^S */
#define ND_SPACE_STR	"\023"
#define ALT_TOG		'\005'		/* ^E (think Extended) */
#define ALT_TOG_STR	"\005"

#define IRCD_BUFFER_SIZE	512
/* Last two bytes are always reserved for \r\n */
#define MAX_PROTOCOL_SIZE	IRCD_BUFFER_SIZE - 2
#define BIG_BUFFER_SIZE		(IRCD_BUFFER_SIZE * 4)

/* 
 * This assumes a channel size less than 10 characters.
 * That should suffice for most non-trivial situations.
 * Otherwise, your privmsg may get truncated...
 * This should be fixed by doing it dynamically.
 */
#ifndef INPUT_BUFFER_SIZE
#define INPUT_BUFFER_SIZE	(IRCD_BUFFER_SIZE - 20)
#endif

#define NICKNAME_LEN 30
#define NAME_LEN 80
#define REALNAME_LEN 50
#define PATH_LEN 1024

/* irc.c's global variables */
extern		int	away_set;
extern		int	background;
extern		int	cpu_saver;
extern		int	current_numeric;
extern		int	dead;
extern volatile	int	dead_children_processes;
extern		int	dumb_mode;
extern		int	foreground;
extern		int	global_beep_ok;
extern		int	inhibit_logging;
extern		int	irc_port;
extern		int	ircrc_loaded;
extern		int	loading_global;
extern		int	oper_command;
extern		int	quick_startup;
extern		int	trusted_output;
extern		int	use_flow_control;
extern		int	who_mask;
extern		int	use_input;
extern		int	waiting_out;
extern		int	waiting_in;
extern		char *	args_str;
extern		char *	cannot_open;
extern const	char *	compile_info;
extern		char *	cut_buffer;
extern		char *	default_channel;
extern		char	empty_string[];
extern		char	space[];
extern		char	star[];
extern		char	dot[];
extern		char	hostname[NAME_LEN + 1];
extern const 	char 	internal_version[];
extern		char *	ircrc_file;
extern		char *	epicrc_file;
extern		char *	startup_file;
extern		char *	irc_lib;
extern		char *	irc_path;
extern const 	char 	irc_version[];
extern		char *	last_notify_nick;
extern		char *	LocalHostName;
extern		char *	my_path;
extern		char	nickname[NICKNAME_LEN + 1];
extern		char	off[];
extern		char	on[];
extern		char	one[];
extern		char	realname[REALNAME_LEN + 1];
extern	const	char	ridiculous_version_name[];
extern	const unsigned long commit_id;
extern		char *	send_umode;
extern	const	char *	unknown_userhost;
extern	const	char 	useful_info[];
extern		char	username[NAME_LEN + 1];
extern		char	userhost[NAME_LEN + 1];
extern		char	zero[];
extern		char	comma[];

extern 		ISA *	LocalIPv4Addr;
#ifdef INET6
extern		ISA6 *	LocalIPv6Addr;
#endif
extern		fd_set  readables, held_readables;
extern		fd_set  writables, held_writables;
extern struct timeval 	start_time;
extern struct timeval	idle_time;
extern struct timeval	now;
extern struct timeval	input_timeout;

/* irc.c's extern functions */
	char	get_a_char 		(void);
	void	get_line_return 	(char, char *);
	void	get_line 		(char *, int, void (*) (char, char *));
	void	io 			(const char *);
	void	irc_exit 		(int, char *, ...) /*__A(2)*/ __N;
	void	irc_quit 		(char, char *);
	void	cpu_saver_off		(void);
#include "reg.h"
	void	dump_load_stack		(int);	 /* XXX command.c */
const	char *	current_filename	(void);  /* XXX command.c */
const	char *	current_loader		(void);	 /* XXX command.c */
	int	current_line		(void);	 /* XXX command.c */
	char *	current_package		(void);  /* XXX command.c */
#endif /* __irc_h */
