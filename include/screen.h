/*
 * screen.h: header for screen.c
 *
 * Copyright 1993 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the copyright file, or type help ircii copyright
 */

#ifndef __screen_h__
#define __screen_h__

/* To get the definition of Window */
#include "window.h"

#define WAIT_PROMPT_LINE        0x01
#define WAIT_PROMPT_KEY         0x02
#define WAIT_PROMPT_DUMMY	0x04

typedef struct PromptStru
{
	char	*prompt;
	char	*data;
	int	type;
	int	echo;
	void	(*func)();
	struct	PromptStru	*next;
}	WaitPrompt;


typedef struct	PhysicalTTY
{
	/* Output stuff */
	FILE	*fpin;			/* The input FILE (eg, stdin) */
	int	fdin;			/* The input FD (eg, 0) */
	FILE	*fpout;			/* The output FILE (eg, stdout) */
	int	fdout;			/* The output FD (eg, 1) */
	int	control;		/* The control FD (to wserv) */
	int	wserv_version;		/* The version of wserv talking to */

	/* Input line and prompt stuff */
	char	input_buffer[INPUT_BUFFER_SIZE+1];
					/* Current user input for us */
	int	buffer_pos;		/* Where on input line cursor is */
	int	buffer_min_pos;		/* First position after prompt */
	int	input_cursor;		/* Where the cursor is on input line */
	int	input_visible;
	int	input_zone_len;
	int	input_start_zone;
	int	input_end_zone;
	char	*input_prompt;
	int	input_prompt_len;
	int	input_prompt_malloc;
	int	input_line;

	char	saved_input_buffer[INPUT_BUFFER_SIZE+1];
	int	saved_buffer_pos;
	int	saved_min_buffer_pos;

	WaitPrompt	*promptlist;

	/* Key qualifier stuff */
	int	quote_hit;		/* True after QUOTE_CHARACTER hit */
	struct timeval last_press;	/* The last time a key was pressed.
					   Used to determine
					   key-independence. */
	struct Key *last_key;		/* The last Key pressed. */

	char	*tty_name;
	int	co;
	int	li;
	int	old_co;
	int	old_li;
}	Pty;


typedef	struct	ScreenStru
{
	/* List stuff and overhead */
	int	screennum;		/* Refnum for this screen */
	Window	*current_window;	/* Current primary window target */
	unsigned last_window_refnum;	/* Most previous current window */
	Window	*window_list;		/* The top window on me */
	Window	*window_list_end;	/* The bottom window on me */
	Window	*cursor_window;		/* The window that has my cursor */
	int	visible_windows;	/* Number of windows on me */
	WindowStack	*window_stack;	/* Number of windows on my stack */
struct	ScreenStru *prev;		/* Next screen in list */
struct	ScreenStru *next;		/* Previous screen in list */

	/* Output stuff */
	FILE	*fpin;			/* The input FILE (eg, stdin) */
	int	fdin;			/* The input FD (eg, 0) */
	FILE	*fpout;			/* The output FILE (eg, stdout) */
	int	fdout;			/* The output FD (eg, 1) */
	int	control;		/* The control FD (to wserv) */
	int	wserv_version;		/* The version of wserv talking to */

	/* Input line and prompt stuff */
	char	input_buffer[INPUT_BUFFER_SIZE+1];
					/* Current user input for us */
	int	buffer_pos;		/* Where on input line cursor is */
	int	buffer_min_pos;		/* First position after prompt */
	int	input_cursor;		/* Where the cursor is on input line */
	int	input_visible;
	int	input_zone_len;
	int	input_start_zone;
	int	input_end_zone;
	char	*input_prompt;
	int	input_prompt_len;
	int	input_prompt_malloc;
	int	input_line;

	char	saved_input_buffer[INPUT_BUFFER_SIZE+1];
	int	saved_buffer_pos;
	int	saved_min_buffer_pos;

	WaitPrompt	*promptlist;

	/* Key qualifier stuff */
	int	quote_hit;		/* True after QUOTE_CHARACTER hit */
	struct timeval last_press;	/* The last time a key was pressed.
					   Used to determine
					   key-independence. */
	struct Key *last_key;		/* The last Key pressed. */


	char	*tty_name;
	int	co;
	int	li;
	int	old_co;
	int	old_li;

	int	alive;
}	Screen;

/* Stuff for the screen/xterm junk */

#define ST_NOTHING      -1
#define ST_SCREEN       0
#define ST_XTERM        1


	void	add_wait_prompt 	(char *, void (*)(), char *, int, int);
	void	set_current_screen 	(Screen *);
	void	window_redirect		(char *, int);
	int	check_screen_redirect	(char *);
	void	add_to_screen		(const unsigned char *);
unsigned char**	split_up_line		(const unsigned char *, int);
	int	output_line		(const unsigned char *);
	void	cursor_not_in_display	(Screen *);
	void	cursor_in_display	(Window *);
	int	is_cursor_in_display	(Screen *);
	void	repaint_one_line	(Window *, int);	/* Don't use */
	void	repaint_window_body	(Window *);
	Screen *create_new_screen	(void);
	Window	*create_additional_screen (void);
	void	kill_screen		(Screen *);
	void	close_all_screen	(void);
	void	do_screens		(fd_set *);

const	char *	all_off			(void);
extern	int	normalize_never_xlate;
extern	int	normalize_permit_all_attributes;
	u_char *normalize_string	(const u_char *, int);
	u_char *denormalize_string	(const u_char *);
	char   *normalize_color		(int, int, int, int);
const	u_char *skip_ctl_c_seq		(const u_char *, int *, int *);
	u_char **prepare_display	(const u_char *, int, int *, int);
	int	output_with_count	(const unsigned char *, int, int);

/* Dont do any word-wrapping, just truncate each line at its place. */
#define PREPARE_NOWRAP	0x01

extern	Screen *main_screen;
extern	Screen *last_input_screen;
extern	Screen *screen_list;
extern	Screen *output_screen;

#endif /* _SCREEN_H_ */
