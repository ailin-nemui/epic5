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

#define WAIT_PROMPT_NONE	0x00
#define WAIT_PROMPT_LINE        0x01
#define WAIT_PROMPT_KEY         0x02
#define WAIT_PROMPT_DUMMY	0x04

	void		repaint_window_body		(int);
	int		create_additional_screen 	(void);
	void		add_wait_prompt 		(const char *, void (*)(char *, const char *), const char *, int, int);
	void		fire_wait_prompt		(uint32_t);
	void		fire_normal_prompt		(const char *);
	void		add_to_screen			(const char *);
	void		translate_user_input		(unsigned char byte);
	void		create_new_screen		(void);
	void		kill_screen			(int);

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

	int		get_screen_bottom_window	(int);
	int		screen_is_valid			(int);
	int     	traverse_all_screens		(int *screen_);

	/* * * */
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
	void *		get_screen_input_line		(int);
	int		get_screen_prompt_list_type	(int);
	int		get_screen_quote_hit		(int);
	Timeval		get_screen_last_press		(int);
	void *		get_screen_last_key		(int);
	int		get_screen_columns		(int);
	int		get_screen_lines		(int);
	int		get_screen_old_columns		(int);
	int		get_screen_old_lines		(int);
	int		get_screen_fixed_windows	(int);

	void		set_screen_alive		(int, int);
	void		set_screen_input_window		(int, int);
	void		set_screen_last_window_refnum	(int, int);
	void		set_screen_window_list		(int, int);
	void		set_screen_visible_windows	(int, int);
	void		set_screen_visible_windows_incr	(int);
	void		set_screen_visible_windows_dec	(int);
	void		set_screen_window_stack		(int, WindowStack *);
	void		set_screen_lines		(int, int);
	void		set_screen_columns		(int, int);
	void		set_screen_old_lines		(int, int);
	void		set_screen_old_columns		(int, int);
	void		set_screen_quote_hit		(int, int);
	void		set_screen_fdin			(int, int);
	void		set_screen_fdout		(int, int);
	void		set_screen_fpin			(int, FILE *);
	void		set_screen_fpout		(int, FILE *);
	void		set_screen_control		(int, int);
	void		set_screen_last_key		(int, void *);
	void		set_screen_last_press		(int, Timeval);
	void		set_screen_input_line		(int, void *);

	int    		screen_add_window_before 	(int screen_, int existing_window_, int new_window_);
	int     	screen_add_window_after 	(int screen_, int existing_window_, int new_window_);
	int     	screen_add_window_first 	(int screen_, int new_window_);
	int     	screen_add_window_last 		(int screen_, int new_window_);
	int     	screen_remove_window 		(int screen_, int old_window_);
	int     	screen_windows_squeeze 		(int screen_);
	int     	screen_windows_make_room_at 	(int screen_, int location);
	int     	screen_window_find 		(int screen_, int window_);
	int     	screen_window_dump 		(int screen_);
	int		screen_window_place		(int screen_, int location, int window_);
	int     	screen_window_swap 		(int screen_, int v_window_, int window_);

	int     	screen_get_window_prev 		(int screen_, int window_);
	int     	screen_get_window_next 		(int screen_, int window_);

/* Dont do any word-wrapping, just truncate each line at its place. */
#define PREPARE_NOWRAP	0x01

extern	int		main_screen;
extern	int		output_screen;
extern	int		last_input_screen;

extern	int		display_line_mangler;

#endif /* _SCREEN_H_ */
