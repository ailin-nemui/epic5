/*
 * timer.h: header for timer.c 
 *
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __timer_h__
#define __timer_h__

	BUILT_IN_COMMAND(timercmd);

	void	ExecuteTimers 	(void);
	char *	add_timer	(int, char *, double, long, int (*) (void *), 
					char *, const char *, Window *);
	int	delete_timer 	(char *);
	struct timeval	TimerTimeout 	(void);
	int	timer_exists	(const char *);

#endif /* _TIMER_H_ */
