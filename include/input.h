/*
 * input.h: header for input.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __input_h__
#define __input_h__

/* miscelaneous functions */
	void	edit_char			(unsigned char);
	void	change_input_prompt 		(int);
	void	cursor_to_input 		(void);
	int	do_input_timeouts		(void *);
	char *	get_input 			(void);
	char *	get_input_prompt 		(void);
	void	init_input 			(void);
	void	input_move_cursor 		(int);
	char	input_pause 			(char *);
	void	set_input 			(char *);
	void	set_input_prompt 		(char *);
	void	update_input 			(int);

/* keybinding functions */
	void 	backward_character 		(char, char *);
	void 	backward_history 		(char, char *);
	void 	clear_screen 			(char, char *);
	void	command_completion 		(char, char *);
	void	cpu_saver_on			(char, char *);
	void 	forward_character		(char, char *);
	void 	forward_history 		(char, char *);
	void	highlight_off 			(char, char *);
	void	input_add_character 		(char, char *);
	void	input_backspace 		(char, char *);
	void	input_backward_word 		(char, char *);
	void	input_beginning_of_line 	(char, char *);
	void	input_clear_line 		(char, char *);
	void	input_clear_to_bol 		(char, char *);
	void	input_clear_to_eol 		(char, char *);
	void	input_delete_character 		(char, char *);
	void	input_delete_next_word		(char, char *);
	void	input_delete_previous_word	(char, char *);
	void	input_delete_to_previous_space	(char, char *);
	void	input_end_of_line		(char, char *);
	void	input_forward_word		(char, char *);
	void	input_transpose_characters	(char, char *);
	void	input_unclear_screen		(char, char *);
	void	input_yank_cut_buffer		(char, char *);
	void	insert_altcharset		(char, char *);
	void	insert_blink			(char, char *);
	void	insert_bold 			(char, char *);
	void	insert_reverse 			(char, char *);
	void	insert_underline 		(char, char *);
	void 	parse_text 			(char, char *);
	void 	quote_char 			(char, char *);
	void	refresh_inputline 		(char, char *);
	void 	send_line 			(char, char *);
	void 	toggle_insert_mode 		(char, char *);
	void 	type_text 			(char, char *);

/* this was in keys.h, but it lives in input.c, so. */
	BUILT_IN_COMMAND(type);
/* used by update_input */
#define NO_UPDATE 0
#define UPDATE_ALL 1
#define UPDATE_FROM_CURSOR 2
#define UPDATE_JUST_CURSOR 3

#endif /* _INPUT_H_ */
