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
	void 	meta1_char 			(char, char *);
	void 	meta2_char 			(char, char *);
	void 	meta3_char 			(char, char *);
	void 	meta4_char 			(char, char *);
	void 	meta5_char 			(char, char *);
	void 	meta6_char 			(char, char *);
	void 	meta7_char 			(char, char *);
	void 	meta8_char 			(char, char *);
	void 	meta9_char 			(char, char *);
	void 	meta10_char 			(char, char *);
	void 	meta11_char 			(char, char *);
	void 	meta12_char 			(char, char *);
	void 	meta13_char 			(char, char *);
	void 	meta14_char 			(char, char *);
	void 	meta15_char 			(char, char *);
	void 	meta16_char 			(char, char *);
	void 	meta17_char 			(char, char *);
	void 	meta18_char 			(char, char *);
	void 	meta19_char 			(char, char *);
	void 	meta20_char 			(char, char *);
	void 	meta21_char 			(char, char *);
	void 	meta22_char 			(char, char *);
	void 	meta23_char 			(char, char *);
	void 	meta24_char 			(char, char *);
	void 	meta25_char 			(char, char *);
	void 	meta26_char 			(char, char *);
	void 	meta27_char 			(char, char *);
	void 	meta28_char 			(char, char *);
	void 	meta29_char 			(char, char *);
	void 	meta30_char 			(char, char *);
	void 	meta31_char 			(char, char *);
	void 	meta32_char 			(char, char *);
	void 	meta33_char 			(char, char *);
	void 	meta34_char 			(char, char *);
	void 	meta35_char 			(char, char *);
	void 	meta36_char 			(char, char *);
	void 	meta37_char 			(char, char *);
	void 	meta38_char 			(char, char *);
	void 	meta39_char 			(char, char *);
	void 	parse_text 			(char, char *);
	void 	quote_char 			(char, char *);
	void	refresh_inputline 		(char, char *);
	void 	send_line 			(char, char *);
	void 	toggle_insert_mode 		(char, char *);
	void 	type_text 			(char, char *);

/* used by update_input */
#define NO_UPDATE 0
#define UPDATE_ALL 1
#define UPDATE_FROM_CURSOR 2
#define UPDATE_JUST_CURSOR 3

#endif /* _INPUT_H_ */
