/*
 * parse.h
 *
 * Copyright 1993 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information.
 */

#ifndef __parse_h__
#define __parse_h__

typedef struct {
	const char	*command;
	void 		(*inbound_handler) (const char *, const char *, const char **);
	void		(*outbound_handler) (char *);
	int		flags;
} protocol_command;
extern 	protocol_command rfc1459[];
extern	int		 num_protocol_cmds;

#define PROTO_NOQUOTE 	1 << 0
#define PROTO_DEPREC	1 << 1


	void    rfc1459_odd 	(const char *, const char *, const char **);
const 	char	*PasteArgs 	(const char **, int);
	void	parse_server 	(const char *);
	int	is_channel	(const char *);

extern	const char	*FromUserHost;
#if 0
extern	int	doing_privmsg;
#endif

#endif

