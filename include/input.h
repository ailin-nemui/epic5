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
	void	change_input_prompt 		(int);
	void	cursor_to_input 		(void);
	char *	get_input 			(void);
	char *	get_input_prompt 		(void);
	void	init_input 			(void);
	void	input_move_cursor 		(int, int);
	char	input_pause 			(char *);
	void	set_input 			(const char *);
	void	set_input_prompt 		(void *);
	void	update_input 			(void *, int);

/* keybinding functions */
	BUILT_IN_KEYBINDING(backward_character);
	BUILT_IN_KEYBINDING(backward_history);
	BUILT_IN_KEYBINDING(clear_screen);
	BUILT_IN_KEYBINDING(command_completion);
	BUILT_IN_KEYBINDING(cpu_saver_on);
	BUILT_IN_KEYBINDING(forward_character);
	BUILT_IN_KEYBINDING(forward_history);
	BUILT_IN_KEYBINDING(highlight_off);
	BUILT_IN_KEYBINDING(input_add_character);
	BUILT_IN_KEYBINDING(input_backspace);
	BUILT_IN_KEYBINDING(input_backward_word);
	BUILT_IN_KEYBINDING(input_beginning_of_line);
	BUILT_IN_KEYBINDING(input_clear_line);
	BUILT_IN_KEYBINDING(input_clear_to_bol);
	BUILT_IN_KEYBINDING(input_clear_to_eol);
	BUILT_IN_KEYBINDING(input_delete_character);
	BUILT_IN_KEYBINDING(input_delete_next_word);
	BUILT_IN_KEYBINDING(input_delete_previous_word);
	BUILT_IN_KEYBINDING(input_delete_to_previous_space);
	BUILT_IN_KEYBINDING(input_end_of_line);
	BUILT_IN_KEYBINDING(input_forward_word);
	BUILT_IN_KEYBINDING(input_reset_line);
	BUILT_IN_KEYBINDING(input_transpose_characters);
	BUILT_IN_KEYBINDING(input_unclear_screen);
	BUILT_IN_KEYBINDING(input_yank_cut_buffer);
	BUILT_IN_KEYBINDING(insert_altcharset);
	BUILT_IN_KEYBINDING(insert_blink);
	BUILT_IN_KEYBINDING(insert_bold);
	BUILT_IN_KEYBINDING(insert_reverse);
	BUILT_IN_KEYBINDING(insert_underline);
	BUILT_IN_KEYBINDING(parse_text);
	BUILT_IN_KEYBINDING(quote_char);
	BUILT_IN_KEYBINDING(refresh_inputline);
	BUILT_IN_KEYBINDING(send_line);
	BUILT_IN_KEYBINDING(toggle_insert_mode);
	BUILT_IN_KEYBINDING(type_text);

/* used by update_input */
#define NO_UPDATE 0
#define UPDATE_ALL 1
#define UPDATE_FROM_CURSOR 2
#define UPDATE_JUST_CURSOR 3
#define CHECK_ZONES 4

#endif /* _INPUT_H_ */
