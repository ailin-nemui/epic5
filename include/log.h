/*
 * log.h: header for log.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __log_h__
#define __log_h__

extern	int	current_log_refnum;

	FILE	*do_log 	(int, const char *, FILE **);
	void	logger 		(void *);
	void	set_logfile 	(void *);
	void	add_to_log 	(int, FILE *, long, const unsigned char *, int, const char *);
	BUILT_IN_COMMAND(logcmd);
	void	add_to_logs	(long, int, const char *, int, const char *);
	char *	logctl		(char *);
	void    logfiles_swap_windows (int oldref, int newref);
	void    logfiles_merge_windows (int oldref, int newref);

#endif /* _LOG_H_ */
