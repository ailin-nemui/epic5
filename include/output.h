/*
 * output.h: header for output.c 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * @(#)$Id: output.h,v 1.4 2003/10/10 06:22:38 jnelson Exp $
 */

#ifndef __output_h__
#define __output_h__

extern	FILE	*irclog_fp;
struct ScreenStru;

	BUILT_IN_COMMAND(extern_write);
	void	put_echo		(const unsigned char *);
	void	put_it 			(const char *, ...) __A(1);
	void	say 			(const char *, ...) __A(1);
	void	yell 			(const char *, ...) __A(1);
	void    privileged_yell 	(const char *, ...) __A(1);

	void	error			(const char *, ...) __A(1);
	SIGNAL_HANDLER(sig_refresh_screen);
	void	refresh_a_screen 	(struct ScreenStru *);
	void	refresh_screen 		(char, char *);
	int	init_screen 		(void);
	void   	file_put_it 		(FILE *fp, const char *format, ...);
#endif /* _OUTPUT_H_ */
