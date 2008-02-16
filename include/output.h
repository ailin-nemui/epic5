/*
 * output.h: header for output.c 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * @(#)$Id: output.h,v 1.9 2008/02/16 03:38:28 jnelson Exp $
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
	void	syserr			(int, const char *, ...) __A(2);

	void	error			(const char *, ...) __A(1);
	SIGNAL_HANDLER(sig_refresh_screen);
	void	redraw_all_screens 	(void);
	BUILT_IN_KEYBINDING(refresh_screen);
	int	init_screen 		(void);
	void   	file_put_it 		(FILE *fp, const char *format, ...);

#endif /* _OUTPUT_H_ */
