/*
 * Global stuff for translation tables.
 *
 * Tomten, tomten@solace.hsh.se / tomten@lysator.liu.se
 *
 * @(#)$Id: translat.h,v 1.4 2004/08/11 23:58:39 jnelson Exp $
 */

#ifndef __translat_h_
# define __translat_h_

extern	void	set_translation (void *);
extern	int	translation;
extern	void	translate_from_server (unsigned char *);
extern	void	translate_to_server (unsigned char *);

#endif /* __translat_h_ */
