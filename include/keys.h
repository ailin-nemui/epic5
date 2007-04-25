/*
 * keys.h: header for keys.c 
 *
 * Copyright 2002 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __keys_h__
#define __keys_h__

	void *	handle_keypress		(void *, Timeval, unsigned char);
	void *	timeout_keypress	(void *, Timeval);
	int	do_input_timeouts	(void *);

BUILT_IN_COMMAND(bindcmd);
BUILT_IN_COMMAND(rbindcmd);
BUILT_IN_COMMAND(parsekeycmd);

	void	init_binds		(void);
	void	init_keys 		(void);
	void	init_termkeys 		(void);
	void	set_key_interval	(void *);
	void	save_bindings 		(FILE *, int);
	void	remove_bindings		(void);
	void	unload_bindings		(const char *);
	void	do_stack_bind		(int, char *);
	char *	bindctl			(char *);

#endif /* __keys_h_ */
