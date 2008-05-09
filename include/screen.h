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
#include "tio.h"

#define WAIT_PROMPT_LINE        0x01
#define WAIT_PROMPT_KEY         0x02
#define WAIT_PROMPT_DUMMY	0x04

typedef struct PromptStru
{
	char	*prompt;
	char	*data;
	int	type;
	int	echo;
	void	(*func) (char *, char *);
	struct	PromptStru	*next;
}	WaitPrompt;

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
#ifdef WITH_THREADED_STDOUT
	tio_file *tio_file;
#endif
	int	control;		/* The control FD (to wserv) */
	int	wserv_version;		/* The version of wserv talking to */

	/* Input line and prompt stuff */
	unsigned char input_buffer[INPUT_BUFFER_SIZE+1];
					/* Current user input for us */
	int	buffer_pos;		/* Where on input line cursor is */
	int	input_cursor;		/* Where the cursor is on input line */
	int	input_visible;
	int	input_zone_len;
	int	input_start_zone;
	unsigned char	*input_prompt;
	int	input_prompt_len;
	int	input_line;

        unsigned char   *ind_left;
        int     ind_left_len;
        unsigned char   *ind_right;
        int     ind_right_len;

        unsigned char	saved_input_buffer[INPUT_BUFFER_SIZE+1];
	int	saved_buffer_pos;

	WaitPrompt	*promptlist;

	/* Key qualifier stuff */
	int	quote_hit;		/* True after QUOTE_CHARACTER hit */
	Timeval last_press;		/* The last time a key was pressed.
					   Used to determine
					   key-independence. */
	void *	last_key;		/* The last Key pressed. */

	char	*tty_name;
	int	co;
	int	li;
	int	old_co;
	int	old_li;

	int	alive;
}	Screen;

	void	add_wait_prompt 	(const char *, void (*)(char *, char *), const char *, int, int);
	void	add_to_screen		(const unsigned char *);
	void	cursor_not_in_display	(struct ScreenStru *);
	void	cursor_in_display	(Window *);
	void	edit_codepoint		(u_32int_t key);
	void	edit_char		(unsigned char);
	int	is_cursor_in_display	(struct ScreenStru *);
	void	repaint_window_body	(Window *);
	void	create_new_screen	(void);
	Window	*create_additional_screen (void);
	void	kill_screen		(struct ScreenStru *);

const	unsigned char *all_off			(void);
	unsigned char *new_normalize_string	(const unsigned char *, int, int);
	unsigned char *denormalize_string	(const unsigned char *);
	unsigned char **prepare_display	(int, const unsigned char *, int, int *, int);
	int	output_with_count	(const unsigned char *, int, int);
	void    add_to_window_scrollback (Window *, const unsigned char *, intmax_t);

	unsigned char *prepare_display2	(const unsigned char *, int, int, char, int);

/* Dont do any word-wrapping, just truncate each line at its place. */
#define PREPARE_NOWRAP	0x01

extern	struct ScreenStru *main_screen;
extern	struct ScreenStru *last_input_screen;
extern	struct ScreenStru *screen_list;
extern	struct ScreenStru *output_screen;
extern	int	display_line_mangler;

#endif /* _SCREEN_H_ */
