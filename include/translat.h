/*
 * Global stuff for translation tables.
 *
 * Tomten, tomten@solace.hsh.se / tomten@lysator.liu.se
 *
 * @(#)$Id: translat.h,v 1.1 2001/10/20 17:19:04 jnelson Exp $
 */

#ifndef __translat_h_
# define __translat_h_

extern	void	set_translation (char *);
extern	unsigned char	transToClient[256];
extern	unsigned char	transFromClient[256];
extern	char	translation;

#endif /* __translat_h_ */
