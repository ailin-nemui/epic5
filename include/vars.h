/*
 * vars.h: header for vars.c
 *
 * Copyright 1990, 1995 Michael Sandroff, Matthew Green and others.
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __vars_h__
#define __vars_h__

/* indexes for the irc_variable array */
enum VAR_TYPES {
	ALLOW_C1_CHARS_VAR,
	ALT_CHARSET_VAR,
	ALWAYS_SPLIT_BIGGEST_VAR,
	AUTO_NEW_NICK_VAR,
	AUTO_RECONNECT_VAR,
	AUTO_RECONNECT_DELAY_VAR,
	AUTO_REJOIN_VAR,
	AUTO_REJOIN_CONNECT_VAR,
	AUTO_REJOIN_DELAY_VAR,
	AUTO_UNMARK_AWAY_VAR,
	AUTO_WHOWAS_VAR,
	BAD_STYLE_VAR,
	BANNER_VAR,
	BANNER_EXPAND_VAR,
	BEEP_VAR,
	BEEP_MAX_VAR,
	BEEP_ON_MSG_VAR,
	BEEP_WHEN_AWAY_VAR,
	BLINK_VIDEO_VAR,
	BOLD_VIDEO_VAR,
	CHANNEL_NAME_WIDTH_VAR,
	CLIENTINFO_VAR,
	CLOCK_VAR,
	CLOCK_24HOUR_VAR,
	CLOCK_FORMAT_VAR,
	CMDCHARS_VAR,
	COLOR_VAR,
	COMMAND_MODE_VAR,
	COMMENT_HACK_VAR,
	CONNECT_TIMEOUT_VAR,
	CONTINUED_LINE_VAR,
	CPU_SAVER_AFTER_VAR,
	CPU_SAVER_EVERY_VAR,
	CURRENT_WINDOW_LEVEL_VAR,
	DCC_AUTO_SEND_REJECTS_VAR,
	DCC_LONG_PATHNAMES_VAR,
	DCC_SLIDING_WINDOW_VAR,
	DCC_STORE_PATH_VAR,
	DCC_TIMEOUT_VAR,
	DCC_USE_GATEWAY_ADDR_VAR,
	DEBUG_VAR,
	DISPATCH_UNKNOWN_COMMANDS_VAR,
	DISPLAY_VAR,
	DISPLAY_ANSI_VAR,
	DISPLAY_PC_CHARACTERS_VAR,
	DO_NOTIFY_IMMEDIATELY_VAR,
	EIGHT_BIT_CHARACTERS_VAR,
	FLOATING_POINT_MATH_VAR,
	FLOOD_AFTER_VAR,
	FLOOD_RATE_VAR,
	FLOOD_USERS_VAR,
	FLOOD_WARNING_VAR,
	FULL_STATUS_LINE_VAR,
	HELP_PAGER_VAR,
	HELP_PATH_VAR,
	HELP_PROMPT_VAR,
	HELP_WINDOW_VAR,
	HIDE_PRIVATE_CHANNELS_VAR,
	HIGHLIGHT_CHAR_VAR,
	HIGH_BIT_ESCAPE_VAR,
	HISTORY_VAR,
	HISTORY_CIRCLEQ_VAR,
	HOLD_INTERVAL_VAR,
	HOLD_MODE_VAR,
	INDENT_VAR,
	INPUT_ALIASES_VAR,
	INPUT_PROMPT_VAR,
	INSERT_MODE_VAR,
	INVERSE_VIDEO_VAR,
	LASTLOG_VAR,
	LASTLOG_LEVEL_VAR,
	LOAD_PATH_VAR,
	LOG_VAR,
	LOGFILE_VAR,
	LOG_REWRITE_VAR,
	MAIL_VAR,
	MANGLE_INBOUND_VAR,
	MANGLE_LOGFILES_VAR,
	MANGLE_OUTBOUND_VAR,
	MAX_RECONNECTS_VAR,
	META_STATES_VAR,
	MIRC_BROKEN_DCC_RESUME_VAR,
	MODE_STRIPPER_VAR,
	ND_SPACE_MAX_VAR,
	NEW_SERVER_LASTLOG_LEVEL_VAR,
	NOTIFY_INTERVAL_VAR,
	NOTIFY_LEVEL_VAR,
	NOTIFY_ON_TERMINATION_VAR,
	NOTIFY_USERHOST_AUTOMATIC_VAR,
	NO_CONTROL_LOG_VAR,
	NO_CTCP_FLOOD_VAR,
	NO_FAIL_DISCONNECT_VAR,
	NUM_OF_WHOWAS_VAR,
	OUTPUT_REWRITE_VAR,
	PAD_CHAR_VAR,
	QUIT_MESSAGE_VAR,
	RANDOM_LOCAL_PORTS_VAR,
	RANDOM_SOURCE_VAR,
	REALNAME_VAR,
	REVERSE_STATUS_LINE_VAR,
	SCREEN_OPTIONS_VAR,
	SCROLL_VAR,
	SCROLLBACK_VAR,
	SCROLLBACK_RATIO_VAR,
	SCROLL_LINES_VAR,
	SECURITY_VAR,
	SHELL_VAR,
	SHELL_FLAGS_VAR,
	SHELL_LIMIT_VAR,
	SHOW_CHANNEL_NAMES_VAR,
	SHOW_END_OF_MSGS_VAR,
	SHOW_NUMERICS_VAR,
	SHOW_STATUS_ALL_VAR,
	SHOW_WHO_HOPCOUNT_VAR,
	SSL_CERTFILE_VAR,
	SSL_KEYFILE_VAR,
	SSL_PATH_VAR,
	STATUS_AWAY_VAR,
	STATUS_CHANNEL_VAR,
	STATUS_CHANOP_VAR,
	STATUS_CLOCK_VAR,
	STATUS_CPU_SAVER_VAR,
	STATUS_DOES_EXPANDOS_VAR,
	STATUS_FORMAT_VAR,
	STATUS_FORMAT1_VAR,
	STATUS_FORMAT2_VAR,
	STATUS_HOLD_VAR,
	STATUS_HOLD_LINES_VAR,
	STATUS_INSERT_VAR,
	STATUS_MAIL_VAR,
	STATUS_MODE_VAR,
	STATUS_NICK_VAR,
	STATUS_NOTIFY_VAR,
	STATUS_NO_REPEAT_VAR,
	STATUS_OPER_VAR,
	STATUS_OVERWRITE_VAR,
	STATUS_QUERY_VAR,
	STATUS_SCROLLBACK_VAR,
	STATUS_SERVER_VAR,
	STATUS_SSL_OFF_VAR,
	STATUS_SSL_ON_VAR,
	STATUS_TRUNCATE_RHS_VAR,
	STATUS_UMODE_VAR,
	STATUS_USER0_VAR,
	STATUS_USER1_VAR,
	STATUS_USER10_VAR,
	STATUS_USER11_VAR,
	STATUS_USER12_VAR,
	STATUS_USER13_VAR,
	STATUS_USER14_VAR,
	STATUS_USER15_VAR,
	STATUS_USER16_VAR,
	STATUS_USER17_VAR,
	STATUS_USER18_VAR,
	STATUS_USER19_VAR,
	STATUS_USER2_VAR,
	STATUS_USER20_VAR,
	STATUS_USER21_VAR,
	STATUS_USER22_VAR,
	STATUS_USER23_VAR,
	STATUS_USER24_VAR,
	STATUS_USER25_VAR,
	STATUS_USER26_VAR,
	STATUS_USER27_VAR,
	STATUS_USER28_VAR,
	STATUS_USER29_VAR,
	STATUS_USER3_VAR,
	STATUS_USER30_VAR,
	STATUS_USER31_VAR,
	STATUS_USER32_VAR,
	STATUS_USER33_VAR,
	STATUS_USER34_VAR,
	STATUS_USER35_VAR,
	STATUS_USER36_VAR,
	STATUS_USER37_VAR,
	STATUS_USER38_VAR,
	STATUS_USER39_VAR,
	STATUS_USER4_VAR,
	STATUS_USER5_VAR,
	STATUS_USER6_VAR,
	STATUS_USER7_VAR,
	STATUS_USER8_VAR,
	STATUS_USER9_VAR,
	STATUS_VOICE_VAR,
	STATUS_WINDOW_VAR,
	SUPPRESS_FROM_REMOTE_SERVER,
	SUPPRESS_SERVER_MOTD_VAR,
	SWITCH_CHANNEL_ON_PART_VAR,
	TAB_VAR,
	TAB_MAX_VAR,
	TERM_DOES_BRIGHT_BLINK_VAR,
	TRANSLATION_VAR,
	TRANSLATION_PATH_VAR,
	UNDERLINE_VIDEO_VAR,
	USER_INFO_VAR,
#define	USERINFO_VAR USER_INFO_VAR
	VERBOSE_CTCP_VAR,
	WORD_BREAK_VAR,
	WSERV_PATH_VAR,
	XTERM_VAR,
	XTERM_OPTIONS_VAR,
	NUMBER_OF_VARIABLES
};

/* var_settings indexes ... also used in display.c for highlights */
#define OFF 			0
#define ON 			1
#define TOGGLE 			2

#define	DEBUG_COMMANDS		0x0001
#define	DEBUG_EXPANSIONS	0x0002
#define DEBUG_FUNCTIONS		0x0004

	BUILT_IN_COMMAND(setcmd);

	int	do_boolean 		(char *, int *);
	int	get_int_var 		(enum VAR_TYPES);
	char	*get_string_var 	(enum VAR_TYPES);
	void	set_int_var		(enum VAR_TYPES, int);
	void	set_string_var		(enum VAR_TYPES, const char *);
	void	init_variables 		(void);
	char	*make_string_var 	(const char *);
	void	set_highlight_char	(char *);
	int	charset_size 		(void);
	void	save_variables 		(FILE *, int);
	void	set_var_value 		(int, char *);
	void	do_stack_set		(int, char *);
	int	parse_mangle		(char *, int, char **);

#endif /* _VARS_H_ */
