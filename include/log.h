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
	void	add_to_log 	(FILE *, unsigned, const unsigned char *);

#endif /* _LOG_H_ */
