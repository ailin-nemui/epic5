/*
 * timer.h: header for timer.c 
 *
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __timer_h__
#define __timer_h__

	BUILT_IN_COMMAND(timercmd);

typedef enum {
	SERVER_TIMER,
	WINDOW_TIMER,
	GENERAL_TIMER
} TimerDomain;
 
	void	ExecuteTimers 	(void);
	char *	add_timer	(int, Char *, double, long, 
				 int (*) (void *), void *, Char *, 
				 TimerDomain, int, int);
	int	timer_exists	(Char *);
	int     remove_timer	(Char *);
	Timeval	TimerTimeout 	(void);
	char *	timerctl	(char *);
	void	dump_timers	(void);
	void    timers_swap_winrefs (int oldref, int newref);


#endif /* _TIMER_H_ */
