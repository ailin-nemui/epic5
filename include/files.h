/*
 * files.h -- header file for files.c
 * Direct file manipulation for irc?  Unheard of!
 *
 * Copyright 1995 Jeremy Nelson
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file for copyright information
 */

#ifndef __files_h__
#define __files_h__

	int	open_file_for_read 	(char *);
	int	open_file_for_write 	(char *);
	int	file_write 		(int, char *);
	int	file_writeb 		(int, char *);
	char *	file_read 		(int);
	char *	file_readb 		(int, int);
	int	file_eof 		(int);
	int	file_close 		(int);
	int	file_valid		(int);
	int	file_rewind		(int);
	int	file_error		(int);
	int	file_seek		(int, long, const char *);
	int	file_skip		(int);

#endif
