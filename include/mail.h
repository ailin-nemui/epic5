/*
 * mail.h: header for mail.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __mail_h__
#define __mail_h__

extern	char	mail_timeref[];

	char	*check_mail 		(void);
	int	check_mail_status 	(void *);
	void	mail_systimer		(void);
	void	set_mail_interval	(int);

#endif /* _MAIL_H_ */
