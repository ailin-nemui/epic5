/*
 * output.h: header for output.c 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * @(#)$Id: output.h,v 1.1.1.1 2000/12/05 00:11:57 jnelson Exp $
 */

#ifndef __output_h__
#define __output_h__

extern	FILE	*irclog_fp;

	BUILT_IN_COMMAND(extern_write);
	void	put_echo		(const unsigned char *);
	void	put_it 			(const char *, ...) __A(1);
	void	say 			(const char *, ...) __A(1);
	void	yell 			(const char *, ...) __A(1);
	void	error			(const char *, ...) __A(1);
	SIGNAL_HANDLER(sig_refresh_screen);
	void	refresh_screen 		(char, char *);
	int	init_screen 		(void);

#endif /* _OUTPUT_H_ */
