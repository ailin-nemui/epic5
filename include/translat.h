/*
 * Global stuff for translation tables.
 *
 * Tomten, tomten@solace.hsh.se / tomten@lysator.liu.se
 *
 * @(#)$Id: translat.h,v 1.2 2001/10/22 01:30:49 jnelson Exp $
 */

#ifndef __translat_h_
# define __translat_h_

extern	void	set_translation (char *);
extern	int	translation;
extern	void	translate_from_server (unsigned char *);
extern	void	translate_to_server (unsigned char *);

#endif /* __translat_h_ */
