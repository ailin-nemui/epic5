/*
 * dcc.h: Things dealing client to client connections. 
 *
 * Copyright 1991 Troy Rollo
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information
 */

#ifndef __dcc_h__
#define __dcc_h__

	void	close_all_dcc 		(void);
	int	dcc_chat_active		(char *);
	void	dcc_chat_transmit 	(char *, char *, const char *, 
						const char *, int);
	void	dcc_check 		(fd_set *);
	void	dcc_list 		(char *);
	char *	dcc_raw_connect 	(char *, u_short, int);
	char *	dcc_raw_listen 		(unsigned short);
	void	dcc_reject 		(char *, char *, char *);
	void	process_dcc 		(char *);
	void	register_dcc_offer 	(char *, char *, char *, char *, 
					 char *, char *, char *);
	char *	DCC_get_current_transfer (void);
	int	dcc_dead		(void);

extern	time_t	dcc_timeout;

#endif /* _DCC_H_ */
