/*
 * log.h: header for log.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __log_h__
#define __log_h__

	FILE	*do_log 	(int, const char *, FILE **);
	void	logger 		(int);
	void	set_log_file 	(const char *);
	void	add_to_log 	(FILE *, unsigned, const unsigned char *, int, const char *);
	BUILT_IN_COMMAND(logcmd);
	void	add_to_logs	(int, int, const char *, int, const char *);

#endif /* _LOG_H_ */
