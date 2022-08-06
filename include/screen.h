/*
 * screen.h: header for screen.c
 *
 * Copyright 1993 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the copyright file, or type help ircii copyright
 */

#ifndef __screen_h__
#define __screen_h__

/* To get the definition of WindowStack */
#include "window.h"

#define WAIT_PROMPT_LINE        0x01
#define WAIT_PROMPT_KEY         0x02
#define WAIT_PROMPT_DUMMY	0x04

typedef struct InputLine
{
	/* The current UTF8 input line (plain old c string */
	char		input_buffer[INPUT_BUFFER_SIZE+1];

	/* The offset into input_buffer where each logical char starts */
	int		logical_chars[INPUT_BUFFER_SIZE + 1];

	/* The logical column in which each logical char lives */
	int		logical_columns[INPUT_BUFFER_SIZE + 1];

	/* Upon which logical char does the cursor sit? */
	int		logical_cursor;

	int		first_display_char;
	int		number_of_logical_chars;

	char *		input_prompt_raw;
	char *		input_prompt;
	int		input_prompt_len;
	int		input_line;

        char *		ind_left;
        int     	ind_left_len;
        char *		ind_right;
        int     	ind_right_len;

	int		refresh;
	int		echo;
}	InputLine;


typedef struct PromptStru
{
struct	PromptStru *	next;

	char *		data;
	int		type;
	void		(*func) (char *, const char *);

	InputLine *	my_input_line;
	InputLine *	saved_input_line;
}	WaitPrompt;

typedef	struct	ScreenStru
{
struct	ScreenStru *	prev;			/* Next screen in list */
struct	ScreenStru *	next;			/* Previous screen in list */
	int		alive;

	/* List stuff and overhead */
	int		screennum;		/* Refnum for this screen */
	int		input_window;		/* Window that has the input focus */
	int	 	last_window_refnum;	/* The previous input window (for /window back) */
	int		_window_list;		/* The top window on me */
	int		visible_windows;	/* Number of windows on me */
	WindowStack *	window_stack;		/* Number of windows on my stack */

	/* Output stuff */
	FILE *		fpin;			/* The input FILE (eg, stdin) */
	int		fdin;			/* The input FD (eg, 0) */
	FILE *		fpout;			/* The output FILE (eg, stdout) */
	int		fdout;			/* The output FD (eg, 1) */
	int		control;		/* The control FD (to wserv) */
	int		wserv_version;		/* The version of wserv talking to */

	InputLine *	il;
	WaitPrompt *	promptlist;

	/* Key qualifier stuff */
	int		quote_hit;		/* True after QUOTE_CHARACTER hit */
	Timeval 	last_press;		/* The last time a key was pressed. */
	void *		last_key;		/* The last Key pressed. */

	int		co;
	int		li;
	int		old_co;
	int		old_li;

}	Screen;

	void		repaint_window_body		(int);
	int		create_additional_screen 	(void);
	void		add_wait_prompt 		(const char *, void (*)(char *, const char *), const char *, int, int);
	void		fire_wait_prompt		(uint32_t);
	void		fire_normal_prompt		(const char *);
	void		add_to_screen			(const char *);
	void		translate_user_input		(unsigned char byte);
	void		create_new_screen		(void);
	void		kill_screen			(struct ScreenStru *);

const	char *		all_off				(void);
	char *		new_normalize_string		(const char *, int, int);
	char *		denormalize_string		(const char *);
	char *		normalized_string_to_plain_text (const char *str);
	char **		prepare_display			(int, const char *, int, int *, int);
	size_t		output_with_count		(const char *, int, int);
	void    	add_to_window_scrollback 	(int, const char *, intmax_t);

	char *		prepare_display2		(const char *, int, int, char, int);

	void		chop_columns 			(char **, size_t);
	void		chop_final_columns 		(char **, size_t);

	int		number_of_windows_on_screen	(struct ScreenStru *);
	int		get_screen_bottom_window	(struct ScreenStru *);

	int     	traverse_all_screens		(int *screen_);
	Screen *	get_screen_by_refnum		(int screen_);

	/* * * */
	Screen *	get_screen_by_refnum		(int);
	int		get_screen_prev			(int);
	int		get_screen_next			(int);
	int		get_screen_alive		(int);
	int		get_screen_screennum		(int);
	int		get_screen_input_window		(int);
	int		get_screen_last_window_refnum	(int);
	int		get_screen_window_list		(int);
	int		get_screen_visible_windows	(int);
	WindowStack *	get_screen_window_stack		(int);
	FILE *		get_screen_fpin			(int);
	int		get_screen_fdin			(int);
	FILE *		get_screen_fpout		(int);
	int		get_screen_fdout		(int);
	int		get_screen_control		(int);
	int		get_screen_wserv_version	(int);
	InputLine *	get_screen_input_line		(int);
	WaitPrompt *	get_screen_prompt_list		(int);
	int		get_screen_quote_hit		(int);
	Timeval		get_screen_last_press		(int);
	void *		get_screen_last_key		(int);
	int		get_screen_columns		(int);
	int		get_screen_lines		(int);
	int		get_screen_old_columns		(int);
	int		get_screen_old_lines		(int);

	void		set_screen_alive		(int, int);
	void		set_screen_input_window		(int, int);
	void		set_screen_last_window_refnum	(int, int);


/* Dont do any word-wrapping, just truncate each line at its place. */
#define PREPARE_NOWRAP	0x01

extern	struct ScreenStru *main_screen;
extern	struct ScreenStru *last_input_screen;
extern	struct ScreenStru *output_screen;
extern	int		display_line_mangler;

#endif /* _SCREEN_H_ */
