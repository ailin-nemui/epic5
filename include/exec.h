/*
 * exec.h: header for exec.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information
 */

#ifndef __exec_h__
#define	__exec_h__

	BUILT_IN_COMMAND(execcmd);
	int	get_child_exit		(pid_t);
	void	clean_up_processes	(void);
	int	text_to_process		(const char *, const char *, int);
	void	add_process_wait	(const char *, const char *);
	int	is_valid_process	(const char *);
	char *	execctl			(char *);

#endif /* _EXEC_H_ */
