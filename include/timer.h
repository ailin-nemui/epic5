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
	char *	add_timer	(int, const char *, double, long, 
				 int (*) (void *), const char *, 
				 const char *, int);
	Timeval	TimerTimeout 	(void);

#endif /* _TIMER_H_ */
