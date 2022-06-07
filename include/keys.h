/*
 * keys.h: header for keys.c 
 *
 * Copyright 2002 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __keys_h__
#define __keys_h__

	void	init_binds		(void);
	void *  handle_keypress         (void *, Timeval, uint32_t, int);
	void    init_keys               (void);
	void    init_termkeys           (void);
	void    remove_bindings         (void);
	void    unload_bindings         (const char *);
	void    set_key_interval        (void *);
	void    do_stack_bind           (int, char *);

BUILT_IN_COMMAND(bindcmd);
BUILT_IN_COMMAND(rbindcmd);
BUILT_IN_COMMAND(parsekeycmd);

	char *	bindctl			(char *);

#endif /* __keys_h_ */
