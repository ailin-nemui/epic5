/*
 * Global stuff for translation tables.
 *
 * Tomten, tomten@solace.hsh.se / tomten@lysator.liu.se
 *
 * @(#)$Id: translat.h,v 1.3 2003/07/22 21:12:53 jnelson Exp $
 */

#ifndef __translat_h_
# define __translat_h_

extern	void	set_translation (const void *);
extern	int	translation;
extern	void	translate_from_server (unsigned char *);
extern	void	translate_to_server (unsigned char *);

#endif /* __translat_h_ */
