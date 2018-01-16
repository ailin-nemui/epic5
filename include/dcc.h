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
	int	dcc_chat_active		(const char *);
	void	dcc_chat_transmit 	(char *, char *, const char *, 
						const char *, int);
	void	dcc_check 		(fd_set *, fd_set *);
	BUILT_IN_COMMAND(dcc_cmd);
	char *	dcc_raw_connect 	(const char *, const char *, int);
	char *	dcc_raw_listen 		(int, unsigned short);
	void	dcc_reject 		(const char *, char *, char *);
	void	register_dcc_offer 	(const char *, char *, char *, char *, 
					 char *, char *, char *, char *);
	char *	DCC_get_current_transfer (void);
	int     wait_for_dcc 		(const char *);

	char	*dccctl			(char *input);

#if 0		/* moved to all-globals.h */
extern	time_t	dcc_timeout;
#endif

#endif /* _DCC_H_ */
